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
 * @file NumericalHTreeTopologyAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Builds numerical H-tree inputs from topology lengths and characterization data.
 */

#include <algorithm>
#include <chrono>
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
#include "numerical_characterization/NumericalCharLibrary.hh"
#include "numerical_characterization/NumericalSample.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"
#include "topology/TopologyGen.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts {
namespace {

struct LevelLengthPlan
{
  std::vector<double> requested_lengths_um;
  std::vector<unsigned> level_length_indices;
  std::vector<unsigned> unique_length_indices;
  double effective_unit_um = 0.0;
  unsigned max_length_idx = 0U;
};

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
    if (requested_length_um > 0.0 && numerical_htree::IsFinite(requested_length_um)) {
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
  if (effective_unit_um <= 0.0 || !numerical_htree::IsFinite(effective_unit_um)) {
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
  if (resolved_options.top_input_slew_ns <= 0.0 || !numerical_htree::IsFinite(resolved_options.top_input_slew_ns)) {
    const auto slew_lattice = char_builder.get_slew_lattice();
    resolved_options.top_input_slew_ns = slew_lattice.isValid() ? slew_lattice.stepValue() : 0.0;
  }
  if (resolved_options.leaf_load_cap_pf <= 0.0 || !numerical_htree::IsFinite(resolved_options.leaf_load_cap_pf)) {
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

}  // namespace

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

}  // namespace icts
