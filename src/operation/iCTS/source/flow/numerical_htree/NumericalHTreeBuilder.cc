// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file NumericalHTreeBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree pattern selection flow built from fitted response models.
 */

#include "numerical_htree/NumericalHTreeBuilder.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ranges>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "Point.hh"
#include "Tree.hh"
#include "characterization/CharBuilder.hh"
#include "characterization/ValueLattice.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "numerical_characterization/FitMetrics.hh"
#include "numerical_characterization/NumericalCharLibrary.hh"
#include "numerical_characterization/NumericalSample.hh"
#include "numerical_characterization/PatternResponseModel.hh"
#include "numerical_characterization/Polynomial2D.hh"
#include "topology/TopologyGen.hh"

namespace icts {
namespace {

constexpr double kEpsilon = 1e-15;

struct EvaluatedState
{
  std::vector<NumericalHTreeLevelResult> levels;
  std::vector<PatternId> pattern_ids;
  double next_input_slew_ns = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double score = 0.0;
  double fanout_multiplier = 1.0;
};

struct LevelLengthPlan
{
  std::vector<double> requested_lengths_um;
  std::vector<unsigned> level_length_indices;
  std::vector<unsigned> unique_length_indices;
  double effective_unit_um = 0.0;
  unsigned max_length_idx = 0U;
};

auto IsFinite(double value) -> bool
{
  return std::isfinite(value);
}

auto MakeFailureResult(const std::string& failure_reason, std::optional<unsigned> failure_level,
                       const std::chrono::steady_clock::time_point& start_time) -> NumericalHTreeResult
{
  NumericalHTreeResult result;
  result.failure_reason = failure_reason;
  result.failure_level = failure_level;
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

auto CalcManhattanDistance(const Point<int>& lhs, const Point<int>& rhs) -> int
{
  return std::abs(lhs.get_x() - rhs.get_x()) + std::abs(lhs.get_y() - rhs.get_y());
}

auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>
{
  std::vector<double> requested_lengths_um;
  const auto levels = topology.levels();
  if (levels.size() <= 1U) {
    return requested_lengths_um;
  }

  requested_lengths_um.reserve(levels.size() - 1U);
  for (std::size_t level_index = 1U; level_index < levels.size(); ++level_index) {
    long long distance_sum = 0;
    std::size_t distance_count = 0U;
    for (const auto node_id : levels[level_index]) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += CalcManhattanDistance(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      continue;
    }

    const auto average_length_dbu = static_cast<double>(distance_sum) / static_cast<double>(distance_count);
    const double requested_length_um = average_length_dbu / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
    if (requested_length_um > 0.0 && IsFinite(requested_length_um)) {
      requested_lengths_um.push_back(requested_length_um);
    }
  }

  return requested_lengths_um;
}

auto CountUniqueLengthBins(const std::vector<double>& requested_lengths_um, double unit_um) -> unsigned
{
  if (requested_lengths_um.empty() || unit_um <= 0.0) {
    return 0U;
  }

  std::vector<unsigned> length_indices;
  length_indices.reserve(requested_lengths_um.size());
  const UniformValueLattice lattice(unit_um, std::numeric_limits<unsigned>::max());
  for (const double length_um : requested_lengths_um) {
    const unsigned length_idx = lattice.coveringIndex(length_um);
    if (length_idx > 0U) {
      length_indices.push_back(length_idx);
    }
  }
  std::ranges::sort(length_indices);
  const auto unique_end = std::ranges::unique(length_indices);
  length_indices.erase(unique_end.begin(), unique_end.end());
  return static_cast<unsigned>(length_indices.size());
}

auto BuildLevelLengthPlan(const std::vector<double>& requested_lengths_um, double preferred_unit_um, unsigned preferred_iterations)
    -> LevelLengthPlan
{
  LevelLengthPlan plan;
  plan.requested_lengths_um = requested_lengths_um;
  if (requested_lengths_um.empty()) {
    return plan;
  }

  const double max_length_um = *std::ranges::max_element(requested_lengths_um);
  const unsigned safe_iterations = std::max(1U, preferred_iterations);
  const auto requested_level_count = static_cast<unsigned>(
      std::min<std::size_t>(requested_lengths_um.size(), static_cast<std::size_t>(std::numeric_limits<unsigned>::max())));
  double effective_unit_um = preferred_unit_um;
  if (effective_unit_um <= 0.0 || !IsFinite(effective_unit_um)) {
    effective_unit_um = max_length_um / static_cast<double>(std::min(safe_iterations, requested_level_count));
  }

  const UniformValueLattice preferred_lattice(effective_unit_um, std::numeric_limits<unsigned>::max());
  unsigned max_idx = preferred_lattice.coveringIndex(max_length_um);
  const bool bins_collapsed = requested_lengths_um.size() > 1U && CountUniqueLengthBins(requested_lengths_um, effective_unit_um) <= 1U;
  if (max_idx > safe_iterations || bins_collapsed) {
    const unsigned target_bins = std::min(safe_iterations, std::max(1U, requested_level_count));
    effective_unit_um = max_length_um / static_cast<double>(target_bins);
    max_idx = UniformValueLattice(effective_unit_um, std::numeric_limits<unsigned>::max()).coveringIndex(max_length_um);
  }

  plan.effective_unit_um = effective_unit_um;
  plan.max_length_idx = std::max(1U, max_idx);
  const UniformValueLattice lattice(plan.effective_unit_um, std::numeric_limits<unsigned>::max());
  plan.level_length_indices.reserve(requested_lengths_um.size());
  for (const double length_um : requested_lengths_um) {
    const unsigned length_idx = lattice.coveringIndex(length_um);
    if (length_idx > 0U) {
      plan.level_length_indices.push_back(length_idx);
      plan.unique_length_indices.push_back(length_idx);
    }
  }
  std::ranges::sort(plan.unique_length_indices);
  const auto unique_end = std::ranges::unique(plan.unique_length_indices);
  plan.unique_length_indices.erase(unique_end.begin(), unique_end.end());
  return plan;
}

auto ResolveDepthCandidates(std::size_t max_depth, const NumericalHTreeOptions& options) -> std::vector<unsigned>
{
  if (max_depth == 0U) {
    return {};
  }

  const unsigned depth_limit = static_cast<unsigned>(std::min<std::size_t>(max_depth, std::numeric_limits<unsigned>::max()));
  if (options.target_depth.has_value()) {
    return {std::clamp(*options.target_depth, 1U, depth_limit)};
  }

  const unsigned requested_window = options.depth_explore_window.value_or(CONFIG_INST.get_htree_depth_explore_window());
  const unsigned resolved_window = std::max(1U, std::min(requested_window, depth_limit));
  std::vector<unsigned> candidates;
  candidates.reserve(resolved_window);
  for (unsigned offset = 0U; offset < resolved_window; ++offset) {
    candidates.push_back(depth_limit - offset);
  }
  return candidates;
}

auto MakeCandidateLevelLengthIndices(const std::vector<unsigned>& full_level_length_indices, unsigned depth) -> std::vector<unsigned>
{
  if (depth == 0U || full_level_length_indices.empty()) {
    return {};
  }

  const std::size_t level_count = std::min<std::size_t>(depth, full_level_length_indices.size());
  return std::vector<unsigned>(full_level_length_indices.begin(),
                               full_level_length_indices.begin() + static_cast<std::ptrdiff_t>(level_count));
}

auto ResolveRuntimeOptions(const NumericalHTreeOptions& options, const CharBuilder& char_builder) -> NumericalHTreeOptions
{
  NumericalHTreeOptions resolved_options = options;
  if (resolved_options.top_input_slew_ns <= 0.0 || !IsFinite(resolved_options.top_input_slew_ns)) {
    const auto slew_lattice = char_builder.get_slew_lattice();
    resolved_options.top_input_slew_ns = slew_lattice.isValid() ? slew_lattice.stepValue() : 0.0;
  }
  if (resolved_options.leaf_load_cap_pf <= 0.0 || !IsFinite(resolved_options.leaf_load_cap_pf)) {
    const auto cap_lattice = char_builder.get_cap_lattice();
    resolved_options.leaf_load_cap_pf = cap_lattice.isValid() ? cap_lattice.maxValue() : 0.0;
  }
  if (!resolved_options.max_model_slew_ns.has_value()) {
    const auto slew_lattice = char_builder.get_slew_lattice();
    if (slew_lattice.isValid()) {
      resolved_options.max_model_slew_ns = slew_lattice.maxValue();
    }
  }
  if (!resolved_options.max_model_load_cap_pf.has_value()) {
    const auto cap_lattice = char_builder.get_cap_lattice();
    if (cap_lattice.isValid()) {
      resolved_options.max_model_load_cap_pf = cap_lattice.maxValue();
    }
  }
  return resolved_options;
}

auto IsMetricUsable(const NumericalHTreeFitMetrics& metrics) -> bool
{
  return metrics.sample_count > 0U && IsFinite(metrics.rmse) && IsFinite(metrics.r2) && IsFinite(metrics.max_abs_error);
}

auto AccumulateMetric(const NumericalHTreeFitMetrics& metrics, NumericalHTreeModelQualitySummary& summary) -> void
{
  if (!IsMetricUsable(metrics)) {
    return;
  }

  if (summary.metric_count == 0U) {
    summary.min_sample_count = metrics.sample_count;
    summary.min_rank = metrics.rank;
    summary.min_r2 = metrics.r2;
    summary.max_rmse = metrics.rmse;
    summary.max_abs_error = metrics.max_abs_error;
  } else {
    summary.min_sample_count = std::min(summary.min_sample_count, metrics.sample_count);
    summary.min_rank = std::min(summary.min_rank, metrics.rank);
    summary.min_r2 = std::min(summary.min_r2, metrics.r2);
    summary.max_rmse = std::max(summary.max_rmse, metrics.rmse);
    summary.max_abs_error = std::max(summary.max_abs_error, metrics.max_abs_error);
  }
  ++summary.metric_count;
}

auto AppendMetric(std::vector<NumericalHTreeModelMetric>& model_metrics, const std::string& label,
                  const std::optional<NumericalHTreeFitMetrics>& metrics) -> void
{
  if (!metrics.has_value() || !IsMetricUsable(*metrics)) {
    return;
  }

  model_metrics.push_back(NumericalHTreeModelMetric{
      .label = label,
      .sample_count = metrics->sample_count,
      .rank = metrics->rank,
      .r2 = metrics->r2,
      .rmse = metrics->rmse,
      .max_abs_error = metrics->max_abs_error,
  });
}

auto CollectModelMetrics(const NumericalHTreeBuildInput& input) -> std::vector<NumericalHTreeModelMetric>
{
  std::vector<NumericalHTreeModelMetric> model_metrics;
  for (std::size_t level_index = 0U; level_index < input.levels.size(); ++level_index) {
    const auto& level = input.levels[level_index];
    for (const auto& model : level.pattern_models) {
      const std::string prefix = model.model_name.empty()
                                     ? "level_" + std::to_string(level_index) + "_pattern_" + std::to_string(model.pattern_id.local_id)
                                     : model.model_name;
      AppendMetric(model_metrics, prefix + ".delay", model.delay_metrics);
      AppendMetric(model_metrics, prefix + ".output_slew", model.output_slew_metrics);
      AppendMetric(model_metrics, prefix + ".driven_cap", model.driven_cap_metrics);
      AppendMetric(model_metrics, prefix + ".power", model.power_metrics);
      AppendMetric(model_metrics, prefix + ".source_boundary_switch_power", model.source_boundary_switch_power_metrics);
    }
  }
  return model_metrics;
}

auto SummarizeModelQuality(const NumericalHTreeBuildInput& input) -> NumericalHTreeModelQualitySummary
{
  if (input.model_quality_summary.has_value()) {
    return *input.model_quality_summary;
  }

  NumericalHTreeModelQualitySummary summary;
  for (const auto& level : input.levels) {
    summary.model_count += level.pattern_models.size();
    for (const auto& model : level.pattern_models) {
      if (model.delay_metrics.has_value()) {
        AccumulateMetric(*model.delay_metrics, summary);
      }
      if (model.output_slew_metrics.has_value()) {
        AccumulateMetric(*model.output_slew_metrics, summary);
      }
      if (model.driven_cap_metrics.has_value()) {
        AccumulateMetric(*model.driven_cap_metrics, summary);
      }
      if (model.power_metrics.has_value()) {
        AccumulateMetric(*model.power_metrics, summary);
      }
      if (model.source_boundary_switch_power_metrics.has_value()) {
        AccumulateMetric(*model.source_boundary_switch_power_metrics, summary);
      }
    }
  }

  summary.available = summary.metric_count > 0U;
  summary.note = summary.available ? "aggregated_from_pattern_model_metrics" : "model_quality_metrics_unavailable";
  return summary;
}

auto ResolveLevelLoadCap(const NumericalHTreeBuildInput& input, std::size_t level_index) -> std::optional<double>
{
  if (level_index >= input.levels.size()) {
    return std::nullopt;
  }

  const double explicit_load_cap_pf = input.levels[level_index].representative_load_cap_pf;
  if (explicit_load_cap_pf > 0.0 && IsFinite(explicit_load_cap_pf)) {
    return explicit_load_cap_pf;
  }

  const double leaf_load_cap_pf = input.options.leaf_load_cap_pf;
  if (leaf_load_cap_pf <= 0.0 || !IsFinite(leaf_load_cap_pf)) {
    return std::nullopt;
  }

  double load_cap_pf = leaf_load_cap_pf;
  if (input.options.max_model_load_cap_pf.has_value() && *input.options.max_model_load_cap_pf > 0.0
      && IsFinite(*input.options.max_model_load_cap_pf)) {
    load_cap_pf = std::min(load_cap_pf, *input.options.max_model_load_cap_pf);
  }
  return load_cap_pf;
}

auto EvaluateRequiredSurface(const NumericalHTreeResponseSurface& surface, double input_slew_ns, double load_cap_pf)
    -> std::optional<double>
{
  const auto value = surface.evaluate(input_slew_ns, load_cap_pf);
  if (!value.has_value() || !IsFinite(*value)) {
    return std::nullopt;
  }
  return value;
}

auto EvaluateOptionalSurface(const NumericalHTreeResponseSurface& surface, double input_slew_ns, double load_cap_pf, double fallback_value)
    -> std::optional<double>
{
  if (!surface.isValid()) {
    return fallback_value;
  }
  const auto value = surface.evaluate(input_slew_ns, load_cap_pf);
  if (!value.has_value() || !IsFinite(*value)) {
    return std::nullopt;
  }
  return value;
}

auto EvaluatePatternModel(const NumericalHTreePatternModel& model, const NumericalHTreeOptions& options, unsigned level_index,
                          double input_slew_ns, double load_cap_pf, double fanout_multiplier) -> std::optional<NumericalHTreeLevelResult>
{
  if (options.max_model_slew_ns.has_value() && *options.max_model_slew_ns > 0.0 && IsFinite(*options.max_model_slew_ns)
      && input_slew_ns > *options.max_model_slew_ns + kEpsilon) {
    return std::nullopt;
  }
  if (options.max_model_load_cap_pf.has_value() && *options.max_model_load_cap_pf > 0.0 && IsFinite(*options.max_model_load_cap_pf)
      && load_cap_pf > *options.max_model_load_cap_pf + kEpsilon) {
    return std::nullopt;
  }

  const auto delay_ns = EvaluateRequiredSurface(model.delay_ns, input_slew_ns, load_cap_pf);
  const auto output_slew_ns = EvaluateRequiredSurface(model.output_slew_ns, input_slew_ns, load_cap_pf);
  const auto driven_cap_pf = EvaluateRequiredSurface(model.driven_cap_pf, input_slew_ns, load_cap_pf);
  const auto power_w = EvaluateRequiredSurface(model.power_w, input_slew_ns, load_cap_pf);
  const auto source_boundary_switch_power_w
      = EvaluateOptionalSurface(model.source_boundary_switch_power_w, input_slew_ns, load_cap_pf, 0.0);

  if (!delay_ns.has_value() || !output_slew_ns.has_value() || !driven_cap_pf.has_value() || !power_w.has_value()
      || !source_boundary_switch_power_w.has_value()) {
    return std::nullopt;
  }

  if (*output_slew_ns <= 0.0 || *driven_cap_pf < 0.0) {
    return std::nullopt;
  }
  if (options.max_model_slew_ns.has_value() && *options.max_model_slew_ns > 0.0 && IsFinite(*options.max_model_slew_ns)
      && *output_slew_ns > *options.max_model_slew_ns + kEpsilon) {
    return std::nullopt;
  }

  if (options.require_non_negative_qor && (*delay_ns < 0.0 || *power_w < 0.0 || *source_boundary_switch_power_w < 0.0)) {
    return std::nullopt;
  }

  NumericalHTreeLevelResult result;
  result.level_index = level_index;
  result.segment_pattern_id = model.pattern_id;
  result.model_name = model.model_name;
  result.input_slew_ns = input_slew_ns;
  result.load_cap_pf = load_cap_pf;
  result.output_slew_ns = *output_slew_ns;
  result.driven_cap_pf = *driven_cap_pf;
  result.delay_ns = *delay_ns;
  result.power_w = *power_w;
  result.source_boundary_switch_power_w = *source_boundary_switch_power_w;
  result.composed_power_contribution_w = level_index == 0U ? *power_w : fanout_multiplier * (*power_w - *source_boundary_switch_power_w);
  return result;
}

auto CalcScore(const NumericalHTreeOptions& options, const EvaluatedState& state) -> double
{
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  if (!state.levels.empty()) {
    output_slew_ns = state.levels.back().output_slew_ns;
    driven_cap_pf = state.levels.back().driven_cap_pf;
  }
  return options.delay_weight * state.delay_ns + options.power_weight * state.power_w + options.output_slew_weight * output_slew_ns
         + options.driven_cap_weight * driven_cap_pf;
}

auto PatternIdLexLess(const std::vector<PatternId>& lhs, const std::vector<PatternId>& rhs) -> bool
{
  const std::size_t common_size = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0U; index < common_size; ++index) {
    const unsigned lhs_pack = lhs[index].pack();
    const unsigned rhs_pack = rhs[index].pack();
    if (lhs_pack != rhs_pack) {
      return lhs_pack < rhs_pack;
    }
  }
  return lhs.size() < rhs.size();
}

auto PreferState(const EvaluatedState& lhs, const EvaluatedState& rhs) -> bool
{
  if (lhs.score != rhs.score) {
    return lhs.score < rhs.score;
  }
  if (lhs.delay_ns != rhs.delay_ns) {
    return lhs.delay_ns < rhs.delay_ns;
  }
  if (lhs.power_w != rhs.power_w) {
    return lhs.power_w < rhs.power_w;
  }
  if (lhs.next_input_slew_ns != rhs.next_input_slew_ns) {
    return lhs.next_input_slew_ns < rhs.next_input_slew_ns;
  }
  return PatternIdLexLess(lhs.pattern_ids, rhs.pattern_ids);
}

auto DelayPowerDominates(const EvaluatedState& lhs, const EvaluatedState& rhs) -> bool
{
  const bool not_worse = lhs.delay_ns <= rhs.delay_ns && lhs.power_w <= rhs.power_w;
  const bool strictly_better = lhs.delay_ns < rhs.delay_ns || lhs.power_w < rhs.power_w;
  return not_worse && strictly_better;
}

auto PreferPowerMedianOrder(const EvaluatedState& lhs, const EvaluatedState& rhs) -> bool
{
  if (lhs.power_w != rhs.power_w) {
    return lhs.power_w < rhs.power_w;
  }
  if (lhs.delay_ns != rhs.delay_ns) {
    return lhs.delay_ns < rhs.delay_ns;
  }
  if (lhs.levels.empty() || rhs.levels.empty()) {
    return lhs.levels.size() < rhs.levels.size();
  }
  const auto& lhs_leaf = lhs.levels.back();
  const auto& rhs_leaf = rhs.levels.back();
  if (lhs_leaf.driven_cap_pf != rhs_leaf.driven_cap_pf) {
    return lhs_leaf.driven_cap_pf < rhs_leaf.driven_cap_pf;
  }
  if (lhs_leaf.output_slew_ns != rhs_leaf.output_slew_ns) {
    return lhs_leaf.output_slew_ns < rhs_leaf.output_slew_ns;
  }
  return PatternIdLexLess(lhs.pattern_ids, rhs.pattern_ids);
}

auto SelectBestFinalState(const std::vector<EvaluatedState>& states) -> const EvaluatedState*
{
  if (states.empty()) {
    return nullptr;
  }

  std::vector<const EvaluatedState*> pareto_front;
  pareto_front.reserve(states.size());
  for (std::size_t state_index = 0U; state_index < states.size(); ++state_index) {
    bool dominated = false;
    for (std::size_t other_index = 0U; other_index < states.size(); ++other_index) {
      if (state_index == other_index) {
        continue;
      }
      if (DelayPowerDominates(states.at(other_index), states.at(state_index))) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_front.push_back(&states.at(state_index));
    }
  }

  if (pareto_front.empty()) {
    return &states.front();
  }
  std::ranges::sort(pareto_front, [](const EvaluatedState* lhs, const EvaluatedState* rhs) -> bool {
    if (lhs == nullptr || rhs == nullptr) {
      return lhs != nullptr;
    }
    return PreferPowerMedianOrder(*lhs, *rhs);
  });
  return pareto_front.at((pareto_front.size() - 1U) / 2U);
}

auto PreferSuccessfulResult(const NumericalHTreeResult& lhs, const NumericalHTreeResult& rhs) -> bool
{
  if (lhs.selected_score != rhs.selected_score) {
    return lhs.selected_score < rhs.selected_score;
  }
  if (lhs.selected_delay_ns != rhs.selected_delay_ns) {
    return lhs.selected_delay_ns < rhs.selected_delay_ns;
  }
  if (lhs.selected_power_w != rhs.selected_power_w) {
    return lhs.selected_power_w < rhs.selected_power_w;
  }
  const unsigned lhs_depth = lhs.selected_depth.value_or(0U);
  const unsigned rhs_depth = rhs.selected_depth.value_or(0U);
  if (lhs_depth != rhs_depth) {
    return lhs_depth < rhs_depth;
  }
  return PatternIdLexLess(lhs.selected_segment_pattern_ids, rhs.selected_segment_pattern_ids);
}

auto BuildFailureResult(const NumericalHTreeBuildInput& input, const std::string& failure_reason, std::optional<unsigned> failure_level,
                        const std::chrono::steady_clock::time_point& start_time) -> NumericalHTreeResult
{
  NumericalHTreeResult result;
  result.failure_reason = failure_reason;
  result.failure_level = failure_level;
  result.model_quality_summary = SummarizeModelQuality(input);
  result.model_metrics = CollectModelMetrics(input);
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

auto RelativeDelta(double lhs, double rhs) -> double
{
  if (rhs <= 0.0 || !IsFinite(lhs) || !IsFinite(rhs)) {
    return std::numeric_limits<double>::infinity();
  }
  return std::max(0.0, lhs - rhs) / rhs;
}

auto ToNumericalBasis(Polynomial2DBasis basis) -> NumericalHTreeResponseBasis
{
  switch (basis) {
    case Polynomial2DBasis::kConstant:
      return NumericalHTreeResponseBasis::kConstant;
    case Polynomial2DBasis::kAffine:
      return NumericalHTreeResponseBasis::kAffine;
    case Polynomial2DBasis::kQuadratic:
      return NumericalHTreeResponseBasis::kQuadratic;
  }
  return NumericalHTreeResponseBasis::kConstant;
}

auto ToSurface(const Polynomial2D& polynomial) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface::normalized(ToNumericalBasis(polynomial.get_basis()), polynomial.get_coefficients(),
                                                   polynomial.get_slew_center_ns(), polynomial.get_slew_scale_ns(),
                                                   polynomial.get_cap_center_pf(), polynomial.get_cap_scale_pf());
}

auto ToFitMetrics(const FitMetrics& metrics) -> std::optional<NumericalHTreeFitMetrics>
{
  if (!metrics.isUsable()) {
    return std::nullopt;
  }
  return NumericalHTreeFitMetrics{
      .sample_count = metrics.sample_count,
      .rank = metrics.rank,
      .rmse = metrics.rmse,
      .r2 = metrics.r2,
      .max_abs_error = metrics.max_abs_error,
  };
}

auto MakePatternModel(const PatternResponseModel& model) -> NumericalHTreePatternModel
{
  return NumericalHTreePatternModel{
      .pattern_id = model.get_pattern_id(),
      .model_name = "pattern_" + std::to_string(model.get_pattern_id().local_id) + "_length_" + std::to_string(model.get_length_idx()),
      .delay_ns = ToSurface(model.get_delay_fit().polynomial),
      .output_slew_ns = ToSurface(model.get_output_slew_fit().polynomial),
      .driven_cap_pf = ToSurface(model.get_driven_cap_fit().polynomial),
      .power_w = ToSurface(model.get_power_fit().polynomial),
      .source_boundary_switch_power_w = ToSurface(model.get_source_boundary_switch_power_fit().polynomial),
      .delay_metrics = ToFitMetrics(model.get_delay_fit().metrics),
      .output_slew_metrics = ToFitMetrics(model.get_output_slew_fit().metrics),
      .driven_cap_metrics = ToFitMetrics(model.get_driven_cap_fit().metrics),
      .power_metrics = ToFitMetrics(model.get_power_fit().metrics),
      .source_boundary_switch_power_metrics = ToFitMetrics(model.get_source_boundary_switch_power_fit().metrics),
  };
}

}  // namespace

NumericalHTreeResponseSurface::NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients)
    : NumericalHTreeResponseSurface(basis, std::move(coefficients), 0.0, 1.0, 0.0, 1.0, false)
{
}

NumericalHTreeResponseSurface::NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients,
                                                             double slew_center_ns, double slew_scale_ns, double cap_center_pf,
                                                             double cap_scale_pf, bool normalize_inputs)
    : _basis(basis),
      _coefficients(std::move(coefficients)),
      _slew_center_ns(slew_center_ns),
      _slew_scale_ns(slew_scale_ns),
      _cap_center_pf(cap_center_pf),
      _cap_scale_pf(cap_scale_pf),
      _normalize_inputs(normalize_inputs)
{
}

auto NumericalHTreeResponseSurface::constant(double value) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kConstant, {value});
}

auto NumericalHTreeResponseSurface::affine(double intercept, double slew_coeff, double cap_coeff) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kAffine, {intercept, slew_coeff, cap_coeff});
}

auto NumericalHTreeResponseSurface::quadratic(double intercept, double slew_coeff, double cap_coeff, double slew_cap_coeff,
                                              double slew_sq_coeff, double cap_sq_coeff) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kQuadratic,
                                       {intercept, slew_coeff, cap_coeff, slew_cap_coeff, slew_sq_coeff, cap_sq_coeff});
}

auto NumericalHTreeResponseSurface::normalized(NumericalHTreeResponseBasis basis, std::vector<double> coefficients, double slew_center_ns,
                                               double slew_scale_ns, double cap_center_pf, double cap_scale_pf)
    -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(basis, std::move(coefficients), slew_center_ns, slew_scale_ns, cap_center_pf, cap_scale_pf, true);
}

auto NumericalHTreeResponseSurface::evaluate(double input_slew_ns, double load_cap_pf) const -> std::optional<double>
{
  if (!isValid() || !IsFinite(input_slew_ns) || !IsFinite(load_cap_pf)) {
    return std::nullopt;
  }

  double slew_value = input_slew_ns;
  double cap_value = load_cap_pf;
  if (_normalize_inputs) {
    if (_slew_scale_ns == 0.0 || _cap_scale_pf == 0.0) {
      return std::nullopt;
    }
    slew_value = (input_slew_ns - _slew_center_ns) / _slew_scale_ns;
    cap_value = (load_cap_pf - _cap_center_pf) / _cap_scale_pf;
  }

  switch (_basis) {
    case NumericalHTreeResponseBasis::kConstant:
      return _coefficients[0U];
    case NumericalHTreeResponseBasis::kAffine:
      return _coefficients[0U] + _coefficients[1U] * slew_value + _coefficients[2U] * cap_value;
    case NumericalHTreeResponseBasis::kQuadratic:
      return _coefficients[0U] + _coefficients[1U] * slew_value + _coefficients[2U] * cap_value + _coefficients[3U] * slew_value * cap_value
             + _coefficients[4U] * slew_value * slew_value + _coefficients[5U] * cap_value * cap_value;
  }

  return std::nullopt;
}

auto NumericalHTreeResponseSurface::isValid() const -> bool
{
  const std::size_t expected_size
      = _basis == NumericalHTreeResponseBasis::kConstant ? 1U : (_basis == NumericalHTreeResponseBasis::kAffine ? 3U : 6U);
  if (_coefficients.size() != expected_size) {
    return false;
  }
  return std::ranges::all_of(_coefficients, [](double coefficient) -> bool { return IsFinite(coefficient); }) && IsFinite(_slew_center_ns)
         && IsFinite(_slew_scale_ns) && IsFinite(_cap_center_pf) && IsFinite(_cap_scale_pf)
         && (!_normalize_inputs || (std::abs(_slew_scale_ns) > kEpsilon && std::abs(_cap_scale_pf) > kEpsilon));
}

auto NumericalHTreeBuilder::build(const NumericalHTreeBuildInput& input) -> NumericalHTreeResult
{
  const auto start_time = std::chrono::steady_clock::now();

  if (input.options.top_k_per_level == 0U) {
    return BuildFailureResult(input, "top_k_per_level_must_be_positive", std::nullopt, start_time);
  }
  if (input.levels.empty()) {
    return BuildFailureResult(input, "missing_htree_levels", std::nullopt, start_time);
  }
  if (input.options.top_input_slew_ns <= 0.0 || !IsFinite(input.options.top_input_slew_ns)) {
    return BuildFailureResult(input, "invalid_top_input_slew", std::nullopt, start_time);
  }

  std::vector<EvaluatedState> states;
  states.push_back(EvaluatedState{
      .levels = {},
      .pattern_ids = {},
      .next_input_slew_ns = input.options.top_input_slew_ns,
      .delay_ns = 0.0,
      .power_w = 0.0,
      .score = 0.0,
      .fanout_multiplier = 1.0,
  });

  NumericalHTreeResult result;
  result.level_candidate_state_counts.reserve(input.levels.size());
  result.level_surviving_state_counts.reserve(input.levels.size());

  for (std::size_t level_index = 0U; level_index < input.levels.size(); ++level_index) {
    const auto load_cap_pf = ResolveLevelLoadCap(input, level_index);
    if (!load_cap_pf.has_value()) {
      return BuildFailureResult(input, "invalid_level_load_cap", static_cast<unsigned>(level_index), start_time);
    }

    const auto& level = input.levels[level_index];
    if (level.pattern_models.empty()) {
      return BuildFailureResult(input, "missing_level_pattern_models", static_cast<unsigned>(level_index), start_time);
    }

    std::vector<EvaluatedState> next_states;
    next_states.reserve(states.size() * level.pattern_models.size());
    for (const auto& state : states) {
      for (const auto& model : level.pattern_models) {
        ++result.evaluated_candidate_count;
        auto level_result = EvaluatePatternModel(model, input.options, static_cast<unsigned>(level_index), state.next_input_slew_ns,
                                                 *load_cap_pf, state.fanout_multiplier);
        if (!level_result.has_value()) {
          continue;
        }
        if (input.options.require_positive_leaf_power && level_index + 1U == input.levels.size() && level_result->power_w <= kEpsilon) {
          continue;
        }

        EvaluatedState next_state = state;
        next_state.levels.push_back(*level_result);
        next_state.pattern_ids.push_back(level_result->segment_pattern_id);
        next_state.next_input_slew_ns = level_result->output_slew_ns;
        next_state.delay_ns += level_result->delay_ns;
        next_state.power_w += level_result->composed_power_contribution_w;
        next_state.fanout_multiplier *= 2.0;
        next_state.score = CalcScore(input.options, next_state);
        if (!IsFinite(next_state.score) || !IsFinite(next_state.power_w) || !IsFinite(next_state.delay_ns)) {
          continue;
        }
        next_states.push_back(std::move(next_state));
      }
    }

    if (next_states.empty()) {
      return BuildFailureResult(input, "no_valid_level_candidates", static_cast<unsigned>(level_index), start_time);
    }

    std::ranges::sort(next_states, PreferState);
    result.level_candidate_state_counts.push_back(next_states.size());
    if (next_states.size() > input.options.top_k_per_level) {
      result.pruned_candidate_count += next_states.size() - input.options.top_k_per_level;
      next_states.resize(input.options.top_k_per_level);
    }
    result.level_surviving_state_counts.push_back(next_states.size());
    states = std::move(next_states);
  }

  std::ranges::sort(states, PreferState);
  const auto* best_state = SelectBestFinalState(states);
  if (best_state == nullptr) {
    return BuildFailureResult(input, "no_final_state_selected", std::nullopt, start_time);
  }

  result.success = true;
  result.selected_depth = static_cast<unsigned>(best_state->levels.size());
  result.selected_levels = best_state->levels.size();
  result.delay_ns = best_state->delay_ns;
  result.power_w = best_state->power_w;
  result.selected_delay_ns = best_state->delay_ns;
  result.selected_power_w = best_state->power_w;
  result.selected_score = best_state->score;
  result.selected_segment_pattern_ids = best_state->pattern_ids;
  result.levels = best_state->levels;
  result.level_results = best_state->levels;
  result.model_metrics = CollectModelMetrics(input);
  result.model_quality_summary = SummarizeModelQuality(input);
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

auto NumericalHTreeBuilder::build(const NumericalCharLibrary& library, const std::vector<unsigned>& level_length_indices,
                                  const NumericalHTreeOptions& options) -> NumericalHTreeResult
{
  NumericalHTreeBuildInput input;
  input.options = options;
  input.levels.reserve(level_length_indices.size());

  for (const unsigned length_idx : level_length_indices) {
    NumericalHTreeLevelInput level;
    for (const auto& model : library.get_models()) {
      if (model.get_length_idx() == length_idx && model.isValid()) {
        level.pattern_models.push_back(MakePatternModel(model));
      }
    }
    input.levels.push_back(std::move(level));
  }

  return build(input);
}

auto NumericalHTreeBuilder::build(const std::vector<Pin*>& loads, const NumericalHTreeOptions& options) -> NumericalHTreeResult
{
  const auto start_time = std::chrono::steady_clock::now();

  if (loads.empty()) {
    return MakeFailureResult("missing_loads", std::nullopt, start_time);
  }

  const auto topology = TopologyGen::build(loads);
  const auto topology_levels = topology.levels();
  if (topology_levels.size() <= 1U) {
    return MakeFailureResult("missing_htree_levels", std::nullopt, start_time);
  }

  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  const auto requested_lengths_um = CollectRequestedLevelLengthsUm(topology, dbu_per_um);
  if (requested_lengths_um.empty()) {
    return MakeFailureResult("empty_level_lengths", std::nullopt, start_time);
  }

  const auto depth_candidates = ResolveDepthCandidates(requested_lengths_um.size(), options);
  if (depth_candidates.empty()) {
    return MakeFailureResult("empty_depth_candidates", std::nullopt, start_time);
  }

  CharBuilder bootstrap_builder;
  bootstrap_builder.init();
  const auto level_length_plan = BuildLevelLengthPlan(requested_lengths_um, bootstrap_builder.get_wire_length_unit_um(),
                                                      bootstrap_builder.get_wire_length_iterations());
  if (level_length_plan.level_length_indices.empty() || level_length_plan.unique_length_indices.empty()
      || level_length_plan.effective_unit_um <= 0.0) {
    return MakeFailureResult("failed_to_resolve_level_length_bins", std::nullopt, start_time);
  }

  CharBuilder::InitOptions char_options;
  char_options.wire_length_unit_um = level_length_plan.effective_unit_um;
  char_options.wire_length_iterations = level_length_plan.max_length_idx;
  char_options.wire_length_indices = level_length_plan.unique_length_indices;

  CharBuilder char_builder;
  char_builder.init(char_options);
  char_builder.build();
  if (char_builder.get_segment_chars().empty()) {
    return MakeFailureResult("numerical_char_builder_produced_no_segment_chars", std::nullopt, start_time);
  }

  const NumericalSampleLattices lattices{
      .slew_lattice = char_builder.get_slew_lattice(),
      .load_cap_lattice = char_builder.get_cap_lattice(),
      .output_slew_lattice = char_builder.get_slew_lattice(),
      .driven_cap_lattice = char_builder.get_cap_lattice(),
      .length_lattice = char_builder.get_length_lattice(),
  };
  const auto library = NumericalCharLibrary::buildFromSegmentChars(char_builder.get_segment_chars(), lattices);
  if (library.empty()) {
    return MakeFailureResult("failed_to_fit_numerical_char_library", std::nullopt, start_time);
  }

  const auto resolved_options = ResolveRuntimeOptions(options, char_builder);
  NumericalHTreeResult selected_result;
  bool has_selected_result = false;
  NumericalHTreeResult last_failure_result;
  for (const unsigned depth : depth_candidates) {
    const auto candidate_level_length_indices = MakeCandidateLevelLengthIndices(level_length_plan.level_length_indices, depth);
    if (candidate_level_length_indices.empty()) {
      last_failure_result = MakeFailureResult("empty_candidate_level_lengths", std::optional<unsigned>{depth}, start_time);
      continue;
    }

    auto candidate_result = build(library, candidate_level_length_indices, resolved_options);
    if (!candidate_result.success) {
      last_failure_result = std::move(candidate_result);
      continue;
    }

    candidate_result.selected_depth = depth;
    candidate_result.selected_levels = candidate_level_length_indices.size();
    if (!has_selected_result || PreferSuccessfulResult(candidate_result, selected_result)) {
      selected_result = std::move(candidate_result);
      has_selected_result = true;
    }
  }

  if (!has_selected_result) {
    auto result = last_failure_result;
    if (result.failure_reason.empty()) {
      result.failure_reason = "no_valid_depth_candidates";
    }
    const auto end_time = std::chrono::steady_clock::now();
    result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
  }

  auto result = std::move(selected_result);
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

auto NumericalHTreeBuilder::compareWithNative(const NumericalHTreeResult& numerical_result,
                                              const NumericalHTreeNativeReference& native_reference, double delay_relative_tolerance,
                                              double power_relative_tolerance) -> NumericalHTreeComparison
{
  NumericalHTreeComparison comparison;
  comparison.numerical_success = numerical_result.success;
  comparison.native_available = native_reference.available;
  comparison.numerical_delay_ns = numerical_result.selected_delay_ns;
  comparison.numerical_power_w = numerical_result.selected_power_w;
  comparison.numerical_runtime_ms = numerical_result.runtime_ms;
  comparison.native_delay_ns = native_reference.delay_ns;
  comparison.native_power_w = native_reference.power_w;
  comparison.native_runtime_ms = native_reference.runtime_ms;
  comparison.numerical_segment_pattern_ids = numerical_result.selected_segment_pattern_ids;
  comparison.native_segment_pattern_ids = native_reference.segment_pattern_ids;

  if (!numerical_result.success || !native_reference.available) {
    return comparison;
  }

  comparison.available = true;
  comparison.relative_delay_delta = RelativeDelta(numerical_result.selected_delay_ns, native_reference.delay_ns);
  comparison.relative_power_delta = RelativeDelta(numerical_result.selected_power_w, native_reference.power_w);
  comparison.delay_within_tolerance = comparison.relative_delay_delta <= delay_relative_tolerance;
  comparison.power_within_tolerance = comparison.relative_power_delta <= power_relative_tolerance;
  comparison.runtime_faster = native_reference.runtime_ms > 0.0 && numerical_result.runtime_ms > 0.0
                              && numerical_result.runtime_ms + kEpsilon < native_reference.runtime_ms;
  return comparison;
}

}  // namespace icts
