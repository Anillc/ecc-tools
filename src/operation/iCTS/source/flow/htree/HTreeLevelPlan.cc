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
 * @file HTreeLevelPlan.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree level length planning and characterization-grid resolution.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "Tree.hh"
#include "ValueLattice.hh"
#include "config/Config.hh"
#include "geometry/Geometry.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"

namespace icts::htree_builder {
namespace {

auto MakeCoveringLengthIndex(double length_um, double length_step_um) -> unsigned
{
  return UniformValueLattice(length_step_um, std::numeric_limits<unsigned>::max()).coveringIndex(length_um);
}

auto MakeDenseLengthIndices(unsigned max_length_idx) -> std::vector<unsigned>
{
  std::vector<unsigned> length_indices;
  length_indices.reserve(max_length_idx);
  for (unsigned length_idx = 1U; length_idx <= max_length_idx; ++length_idx) {
    length_indices.push_back(length_idx);
  }
  return length_indices;
}

}  // namespace

auto CountUniqueAlignedLengthBins(const std::vector<double>& requested_lengths_um, double length_step_um) -> unsigned
{
  if (requested_lengths_um.empty() || length_step_um <= 0.0) {
    return 0U;
  }

  std::vector<unsigned> aligned_bins;
  aligned_bins.reserve(requested_lengths_um.size());
  for (const double requested_length_um : requested_lengths_um) {
    const unsigned aligned_idx = MakeCoveringLengthIndex(requested_length_um, length_step_um);
    if (aligned_idx > 0U) {
      aligned_bins.push_back(aligned_idx);
    }
  }

  if (aligned_bins.empty()) {
    return 0U;
  }

  std::ranges::sort(aligned_bins);
  const auto unique_tail = std::ranges::unique(aligned_bins);
  aligned_bins.erase(unique_tail.begin(), unique_tail.end());
  return static_cast<unsigned>(aligned_bins.size());
}

auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>
{
  std::vector<double> requested_lengths_um;
  const auto levels = topology.levels();
  if (levels.size() <= 1U) {
    return requested_lengths_um;
  }

  requested_lengths_um.reserve(levels.size() - 1U);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    long long distance_sum = 0;
    std::size_t distance_count = 0;
    for (const auto node_id : levels.at(level)) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += geometry::Manhattan(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      continue;
    }

    const int requested_length_dbu
        = static_cast<int>(std::llround(static_cast<double>(distance_sum) / static_cast<double>(distance_count)));
    const double requested_length_um
        = static_cast<double>(std::max(requested_length_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
    if (requested_length_um > 0.0) {
      requested_lengths_um.push_back(requested_length_um);
    }
  }

  return requested_lengths_um;
}

auto ResolveCharacterizationGridPlan(const Tree& topology, int32_t dbu_per_um) -> CharacterizationGridPlan
{
  CharacterizationGridPlan plan;
  const auto requested_lengths_um = CollectRequestedLevelLengthsUm(topology, dbu_per_um);
  if (requested_lengths_um.empty()) {
    return plan;
  }
  plan.requested_level_lengths = static_cast<unsigned>(requested_lengths_um.size());

  const double configured_unit_um = CONFIG_INST.get_wirelength_unit_um();
  plan.configured_wirelength_iterations = std::max(1U, CONFIG_INST.get_wirelength_iterations());
  plan.configured_wirelength_unit_um = configured_unit_um;
  plan.configured_wirelength_missing = configured_unit_um <= 0.0;

  double effective_unit_um = configured_unit_um;
  if (effective_unit_um > 0.0) {
    plan.unique_level_bins = CountUniqueAlignedLengthBins(requested_lengths_um, effective_unit_um);
    plan.source = CharGridSource::kRuntimeConfig;
  }

  const double max_requested_length_um = *std::ranges::max_element(requested_lengths_um);
  const double fallback_unit_um = max_requested_length_um / static_cast<double>(requested_lengths_um.size());
  const bool grid_collapsed = configured_unit_um > 0.0 && requested_lengths_um.size() > 1U && plan.unique_level_bins <= 1U;
  plan.configured_grid_collapsed = grid_collapsed;
  if (plan.configured_wirelength_missing || grid_collapsed) {
    effective_unit_um = fallback_unit_um;
    plan.adapted = effective_unit_um > 0.0;
    plan.source = plan.adapted ? CharGridSource::kAutoDerived : CharGridSource::kNone;
    plan.auto_derived_wirelength_unit_um = effective_unit_um;
    plan.unique_level_bins = CountUniqueAlignedLengthBins(requested_lengths_um, effective_unit_um);
  }

  if (!plan.adapted || effective_unit_um <= 0.0) {
    return plan;
  }

  plan.wirelength_unit_um = effective_unit_um;
  plan.required_covering_iterations = std::max(1U, static_cast<unsigned>(std::ceil(max_requested_length_um / effective_unit_um)));
  plan.wirelength_iterations = std::min(plan.configured_wirelength_iterations, plan.required_covering_iterations);
  return plan;
}

auto BuildLevelPlans(const Tree& topology, double length_step_um, int32_t dbu_per_um) -> std::vector<HTreeBuilder::LevelPlan>
{
  std::vector<HTreeBuilder::LevelPlan> plans;
  const auto levels = topology.levels();
  if (levels.size() <= 1U || length_step_um <= 0.0) {
    return plans;
  }

  plans.reserve(levels.size() - 1U);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    long long distance_sum = 0;
    std::size_t distance_count = 0;
    for (const auto node_id : levels.at(level)) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += geometry::Manhattan(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      plans.push_back({});
      continue;
    }

    const int requested_length_dbu
        = static_cast<int>(std::llround(static_cast<double>(distance_sum) / static_cast<double>(distance_count)));
    const double requested_length_um
        = static_cast<double>(std::max(requested_length_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
    const unsigned aligned_length_idx = MakeCoveringLengthIndex(requested_length_um, length_step_um);
    plans.push_back(HTreeBuilder::LevelPlan{
        .requested_length_dbu = requested_length_dbu,
        .requested_length_um = requested_length_um,
        .aligned_length_idx = aligned_length_idx,
        .aligned_length_um = static_cast<double>(aligned_length_idx) * length_step_um,
        .is_leaf_level = (level + 1U == levels.size()),
        .selected_has_any_buffer = false,
        .selected_has_terminal_branch_buffer = false,
        .selected_leaf_buffer_cell_master = {},
        .selected_terminal_cell_master = {},
        .segment_pattern_id = PatternId::segment(0),
    });
  }

  return plans;
}

auto ResolveDirectCharacterizationLengthIndices(const Tree& topology, const CharacterizationGridPlan& char_grid_plan, int32_t dbu_per_um)
    -> std::vector<unsigned>
{
  if (!char_grid_plan.adapted || char_grid_plan.wirelength_iterations == 0U) {
    return {};
  }

  if (char_grid_plan.required_covering_iterations > char_grid_plan.wirelength_iterations) {
    return MakeDenseLengthIndices(char_grid_plan.wirelength_iterations);
  }

  auto required_length_indices = CollectRequiredLengthIndices(BuildLevelPlans(topology, char_grid_plan.wirelength_unit_um, dbu_per_um));
  std::erase_if(required_length_indices, [&](unsigned length_idx) -> bool { return length_idx > char_grid_plan.wirelength_iterations; });
  return required_length_indices;
}

auto MakeCandidateLevelPlans(const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth)
    -> std::vector<HTreeBuilder::LevelPlan>
{
  std::vector<HTreeBuilder::LevelPlan> candidate_levels;
  if (depth == 0U || full_level_plans.empty()) {
    return candidate_levels;
  }

  const std::size_t level_count = std::min<std::size_t>(depth, full_level_plans.size());
  candidate_levels.reserve(level_count);
  for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
    auto level = full_level_plans.at(level_index);
    level.is_leaf_level = (level_index + 1U == level_count);
    level.selected_has_any_buffer = false;
    level.selected_has_terminal_branch_buffer = false;
    level.selected_leaf_buffer_cell_master.clear();
    level.selected_terminal_cell_master.clear();
    level.segment_pattern_id = PatternId::segment(0);
    candidate_levels.push_back(std::move(level));
  }

  return candidate_levels;
}

auto CountCandidateLeafNodes(const Tree& topology, unsigned depth) -> std::size_t
{
  const auto topology_levels = topology.levels();
  if (depth == 0U || depth >= topology_levels.size()) {
    return 0U;
  }
  return topology_levels.at(depth).size();
}

auto ResolveDepthCandidates(unsigned max_depth, const HTreeBuilder::BuildOptions& options) -> std::vector<unsigned>
{
  if (max_depth == 0U) {
    return {};
  }

  if (options.target_depth.has_value()) {
    const unsigned resolved_depth = std::clamp(*options.target_depth, 1U, max_depth);
    return {resolved_depth};
  }

  const unsigned requested_window = options.depth_explore_window.value_or(CONFIG_INST.get_htree_depth_explore_window());
  const unsigned resolved_window = std::max(1U, std::min(requested_window, max_depth));

  std::vector<unsigned> candidates;
  candidates.reserve(resolved_window);
  for (unsigned offset = 0U; offset < resolved_window; ++offset) {
    candidates.push_back(max_depth - offset);
  }
  return candidates;
}

}  // namespace icts::htree_builder
