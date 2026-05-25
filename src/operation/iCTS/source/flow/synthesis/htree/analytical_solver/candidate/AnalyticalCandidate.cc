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
 * @file AnalyticalCandidate.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Analytical H-tree candidate and DP label helpers.
 */

#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "AnalyticalModel.hh"
#include "CharCore.hh"
#include "ValueLattice.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree::analytical_solver {
namespace {

auto ApproxLessOrEqual(double lhs, double rhs, double epsilon) -> bool
{
  return lhs <= rhs + epsilon;
}

auto ResolveMergedSourceLoadCount(const PatternCompositionState& upstream_state, const PatternCompositionState& downstream_state)
    -> std::size_t
{
  if (upstream_state.monotonic_boundary_state.source.has_buffer) {
    return 1U;
  }
  if (downstream_state.source_exposed_load_count > std::numeric_limits<std::size_t>::max() / 2U) {
    return std::numeric_limits<std::size_t>::max();
  }
  return downstream_state.source_exposed_load_count * 2U;
}

auto BuildSeedTopologyPattern(TopologyPatternLibrary& library, PatternId topology_pattern_id, PatternId segment_pattern_id,
                              const PatternCompositionState& composition_state) -> void
{
  library.addSeed(topology_pattern_id, segment_pattern_id, composition_state);
}

}  // namespace

auto AnalyticalCandidate::isValid() const -> bool
{
  return rejection_reason.empty() && branch_buffer_legal && fanout_legal && !level_segment_pattern_ids.empty();
}

auto LexicographicalPatternIdLess(const std::vector<PatternId>& lhs, const std::vector<PatternId>& rhs) -> bool
{
  return std::ranges::lexicographical_compare(lhs, rhs, [](PatternId left, PatternId right) -> bool { return left.pack() < right.pack(); });
}

auto PreferAnalyticalCandidate(const AnalyticalCandidate& lhs, const AnalyticalCandidate& rhs) -> bool
{
  if (lhs.conservative_delay_ns != rhs.conservative_delay_ns) {
    return lhs.conservative_delay_ns < rhs.conservative_delay_ns;
  }
  if (lhs.conservative_power_w != rhs.conservative_power_w) {
    return lhs.conservative_power_w < rhs.conservative_power_w;
  }
  if (lhs.root_source_cap_pf != rhs.root_source_cap_pf) {
    return lhs.root_source_cap_pf < rhs.root_source_cap_pf;
  }
  return LexicographicalPatternIdLess(lhs.level_segment_pattern_ids, rhs.level_segment_pattern_ids);
}

auto BuildAnalyticalTopologyPattern(const std::vector<PatternId>& level_segment_pattern_ids,
                                    const BufferPatternLibrary& segment_pattern_library, std::size_t max_fanout)
    -> std::optional<TopologyPatternLibrary>
{
  TopologyPatternLibrary library;
  if (level_segment_pattern_ids.empty()) {
    return std::nullopt;
  }

  unsigned next_pattern_id = 0U;
  PatternId current_pattern_id = PatternId::topology(next_pattern_id++);
  BuildSeedTopologyPattern(library, current_pattern_id, level_segment_pattern_ids.back(),
                           segment_pattern_library.getCompositionState(level_segment_pattern_ids.back()));

  for (std::size_t reverse_index = level_segment_pattern_ids.size() - 1U; reverse_index > 0U; --reverse_index) {
    const PatternId upstream_topology_id = PatternId::topology(next_pattern_id++);
    const PatternId upstream_segment_id = level_segment_pattern_ids.at(reverse_index - 1U);
    BuildSeedTopologyPattern(library, upstream_topology_id, upstream_segment_id,
                             segment_pattern_library.getCompositionState(upstream_segment_id));

    TopologyPatternLibraryCombiner combiner(library, next_pattern_id, max_fanout);
    if (!combiner.canCompose(upstream_topology_id, current_pattern_id)) {
      return std::nullopt;
    }
    current_pattern_id = combiner.combine(upstream_topology_id, current_pattern_id);
    next_pattern_id = combiner.get_next_id();
  }
  return library;
}

auto MaterializeAnalyticalTopologyChar(const AnalyticalCandidate& candidate, const icts::UniformValueLattice& slew_lattice,
                                       const icts::UniformValueLattice& cap_lattice) -> std::optional<HTreeTopologyChar>
{
  if (!candidate.isValid() || !slew_lattice.isValid() || !cap_lattice.isValid()) {
    return std::nullopt;
  }

  const auto input_slew_idx = slew_lattice.tryObservedIndex(std::max(candidate.root_input_slew_ns, slew_lattice.stepValue()));
  const auto output_slew_idx = slew_lattice.tryObservedIndex(std::max(candidate.output_slew_ns, slew_lattice.stepValue()));
  const auto driven_cap_idx = cap_lattice.tryObservedIndex(std::max(candidate.root_source_cap_pf, cap_lattice.stepValue()));
  const auto load_cap_idx = cap_lattice.tryObservedIndex(std::max(candidate.leaf_load_cap_pf, cap_lattice.stepValue()));
  if (!input_slew_idx.has_value() || !output_slew_idx.has_value() || !driven_cap_idx.has_value() || !load_cap_idx.has_value()) {
    return std::nullopt;
  }

  const PatternId topology_pattern_id = PatternId::topology(
      candidate.topology_pattern_library.nodes.empty() ? 0U : static_cast<unsigned>(candidate.topology_pattern_library.nodes.size() - 1U));
  CharCore core(*input_slew_idx, *output_slew_idx, *driven_cap_idx, *load_cap_idx, candidate.raw_delay_ns, candidate.raw_power_w,
                topology_pattern_id, 0.0);
  return HTreeTopologyChar(std::move(core), candidate.depth);
}

auto PrependAnalyticalDpSegment(const AnalyticalDpLabel& suffix, const icts::analytical::AnalyticalModelSet& model_set,
                                const PatternCompositionState& segment_state, const AnalyticalDpTransitionConfig& config)
    -> std::optional<AnalyticalDpLabel>
{
  if (!model_set.isComplete() || !model_set.source_cap_operator.has_value() || config.leaf_load_cap_pf <= 0.0
      || config.input_slew_probe_ns <= 0.0) {
    return std::nullopt;
  }
  if (!suffix.cap_operator.isValid()) {
    return std::nullopt;
  }
  const auto source_cap_operator = model_set.source_cap_operator.value();
  const auto* output_slew_model = model_set.findMetric(icts::analytical::AnalyticalMetric::kOutputSlew);
  if (output_slew_model == nullptr) {
    return std::nullopt;
  }

  const auto branch_operator = icts::analytical::StructuralCapOperator::fanout(config.branch_fanout, config.branch_junction_cap_pf);
  const double suffix_cap_pf = suffix.cap_operator.apply(config.leaf_load_cap_pf);
  const double downstream_cap_pf = branch_operator.apply(suffix_cap_pf);
  const auto eval_metric = [&](icts::analytical::AnalyticalMetric metric) -> std::optional<double> {
    const auto* model = model_set.findMetric(metric);
    if (model == nullptr) {
      return std::nullopt;
    }
    return config.use_conservative_metrics ? model->evaluateConservativeUpper(config.input_slew_probe_ns, downstream_cap_pf)
                                           : model->evaluate(config.input_slew_probe_ns, downstream_cap_pf);
  };

  const auto output_slew = eval_metric(icts::analytical::AnalyticalMetric::kOutputSlew);
  const auto delay = eval_metric(icts::analytical::AnalyticalMetric::kDelay);
  const auto power = eval_metric(icts::analytical::AnalyticalMetric::kPower);
  const auto source_boundary_power = eval_metric(icts::analytical::AnalyticalMetric::kSourceBoundaryNetSwitchPower);
  if (!output_slew.has_value() || !delay.has_value() || !power.has_value() || !source_boundary_power.has_value()) {
    return std::nullopt;
  }
  if (suffix.input_slew_min_ns > 0.0 && *output_slew < suffix.input_slew_min_ns) {
    return std::nullopt;
  }
  if (suffix.input_slew_max_ns > 0.0 && *output_slew > suffix.input_slew_max_ns) {
    return std::nullopt;
  }

  AnalyticalDpLabel label;
  label.cap_operator = icts::analytical::StructuralCapOperator::compose(
      source_cap_operator, icts::analytical::StructuralCapOperator::compose(branch_operator, suffix.cap_operator));
  label.input_slew_min_ns = output_slew_model->domain.slew_min_ns;
  label.input_slew_max_ns = output_slew_model->domain.slew_max_ns;
  label.delay_lower_ns = *delay + suffix.delay_lower_ns;
  label.delay_upper_ns = *delay + suffix.delay_upper_ns;
  const double owned_power = *power - config.source_boundary_power_weight * *source_boundary_power;
  label.power_lower_w = owned_power + suffix.power_lower_w;
  label.power_upper_w = owned_power + suffix.power_upper_w;
  label.monotonic_boundary_state = MonotonicBoundaryState::compose(segment_state.monotonic_boundary_state, suffix.monotonic_boundary_state);
  label.source_exposed_load_count
      = ResolveMergedSourceLoadCount(segment_state, PatternCompositionState{
                                                        .terminal_semantic = TerminalSemantic::kLeafUnbuffered,
                                                        .monotonic_boundary_state = suffix.monotonic_boundary_state,
                                                        .source_exposed_load_count = suffix.source_exposed_load_count,
                                                    });
  label.trace_segment_pattern_ids = suffix.trace_segment_pattern_ids;
  label.trace_segment_pattern_ids.insert(label.trace_segment_pattern_ids.begin(), model_set.key.pattern_id);
  return label;
}

auto DominatesIntervalSafe(const AnalyticalDpLabel& lhs, const AnalyticalDpLabel& rhs, const AnalyticalDominanceConfig& config) -> bool
{
  const bool delay_dominates = ApproxLessOrEqual(lhs.delay_upper_ns, rhs.delay_lower_ns, config.delay_epsilon);
  const bool power_dominates = ApproxLessOrEqual(lhs.power_upper_w, rhs.power_lower_w, config.power_epsilon);
  const bool cap_dominates = ApproxLessOrEqual(lhs.cap_operator.apply(0.0), rhs.cap_operator.apply(0.0), config.cap_epsilon)
                             && ApproxLessOrEqual(lhs.cap_operator.alpha, rhs.cap_operator.alpha, config.cap_epsilon);
  const bool strict = lhs.delay_upper_ns < rhs.delay_lower_ns - config.delay_epsilon
                      || lhs.power_upper_w < rhs.power_lower_w - config.power_epsilon
                      || lhs.cap_operator.apply(0.0) < rhs.cap_operator.apply(0.0) - config.cap_epsilon;
  return delay_dominates && power_dominates && cap_dominates && strict;
}

auto CompressParetoLabels(std::vector<AnalyticalDpLabel> labels, const AnalyticalDominanceConfig& config) -> std::vector<AnalyticalDpLabel>
{
  std::ranges::sort(labels, [](const AnalyticalDpLabel& lhs, const AnalyticalDpLabel& rhs) -> bool {
    if (lhs.delay_upper_ns != rhs.delay_upper_ns) {
      return lhs.delay_upper_ns < rhs.delay_upper_ns;
    }
    if (lhs.power_upper_w != rhs.power_upper_w) {
      return lhs.power_upper_w < rhs.power_upper_w;
    }
    if (lhs.cap_operator.apply(0.0) != rhs.cap_operator.apply(0.0)) {
      return lhs.cap_operator.apply(0.0) < rhs.cap_operator.apply(0.0);
    }
    return LexicographicalPatternIdLess(lhs.trace_segment_pattern_ids, rhs.trace_segment_pattern_ids);
  });

  std::vector<AnalyticalDpLabel> frontier;
  frontier.reserve(labels.size());
  for (const auto& label : labels) {
    bool dominated = false;
    for (const auto& kept : frontier) {
      if (DominatesIntervalSafe(kept, label, config)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      frontier.push_back(label);
    }
  }
  if (config.max_labels > 0U && frontier.size() > config.max_labels) {
    frontier.resize(config.max_labels);
  }
  return frontier;
}

}  // namespace icts::htree::analytical_solver
