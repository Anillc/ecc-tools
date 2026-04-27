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
 * @file HTreeBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief End-to-end H-tree synthesis flow built from topology and characterization modules.
 */

#include "htree/HTreeBuilder.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Log.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
#include "characterization/CharBuilder.hh"
#include "config/Config.hh"
#include "htree/HTreeBuilderInternal.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "topology/TopologyGen.hh"

namespace icts {

namespace {

auto ResolveSelectedRootDriverCellMaster(const std::vector<HTreeBuilder::LevelPlan>& levels) -> std::string
{
  for (const auto& level : levels) {
    if (!level.selected_leaf_buffer_cell_master.empty()) {
      return level.selected_leaf_buffer_cell_master;
    }
    if (!level.selected_terminal_cell_master.empty()) {
      return level.selected_terminal_cell_master;
    }
  }
  return {};
}

}  // namespace

auto HTreeBuilder::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto HTreeBuilder::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  result.root_net = &root_net;
  result.root_output_pin = root_net.get_driver();
  result.root_inst = result.root_output_pin == nullptr ? nullptr : result.root_output_pin->get_inst();
  if (result.root_output_pin == nullptr) {
    result.failure_reason = "missing_root_driver_pin";
    LOG_WARNING << "HTreeBuilder: build skipped because root net " << root_net.get_name() << " has no driver pin.";
    return result;
  }

  const auto loads = root_net.get_loads();
  if (loads.empty()) {
    result.failure_reason = "empty_root_net_loads";
    LOG_WARNING << "HTreeBuilder: build skipped because root net " << root_net.get_name() << " has no loads.";
    return result;
  }
  schema::ScopedStage build_stage("HTreeBuilder", "build");

  BiPartitionConfig topology_config;
  topology_config.htree_topology_tolerance
      = std::max(0.0, options.htree_topology_tolerance.value_or(CONFIG_INST.get_htree_topology_tolerance()));
  result.topology = TopologyGen::build(loads, topology_config);
  const auto levels = result.topology.levels();
  if (levels.size() <= 1U) {
    LOG_WARNING << "HTreeBuilder: topology has no H-tree levels after generation.";
    build_stage.skip({{"reason", "no_h_tree_levels"}});
    return result;
  }

  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  build_stage.markRunning("characterization");
  CharBuilder char_builder;
  const auto char_flow = htree_builder::RunCharacterizationFlow(result.topology, dbu_per_um, result, char_builder);
  if (!char_flow.success) {
    build_stage.skip({{"reason", char_flow.failure_reason}});
    return result;
  }

  const auto base_resolved_options = htree_builder::ResolveBuildOptions(options, char_builder);
  result.force_branch_buffer = base_resolved_options.force_branch_buffer;
  result.target_depth = options.target_depth;

  const auto full_level_plans = htree_builder::BuildLevelPlans(result.topology, char_flow.length_step_um, dbu_per_um);
  if (full_level_plans.empty()) {
    LOG_WARNING << "HTreeBuilder: failed to derive H-tree level plans from topology.";
    build_stage.skip({{"reason", "empty_level_plans"}});
    return result;
  }

  const auto max_depth = static_cast<unsigned>(full_level_plans.size());
  const auto depth_candidates = htree_builder::ResolveDepthCandidates(max_depth, options);
  if (depth_candidates.empty()) {
    LOG_WARNING << "HTreeBuilder: no depth candidates were resolved from topology.";
    build_stage.skip({{"reason", "empty_depth_candidates"}});
    return result;
  }
  result.depth_explore_window = static_cast<unsigned>(depth_candidates.size());

  htree_builder::BufferPatternRegistry segment_pattern_registry;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_registry.add(pattern);
  }

  const auto required_length_indices = htree_builder::CollectRequiredLengthIndices(full_level_plans);
  auto entry_sets_by_length
      = htree_builder::SynthesizeSegmentEntrySets(char_builder.get_segment_chars(), segment_pattern_registry, required_length_indices);
  if (entry_sets_by_length.empty()) {
    LOG_WARNING << "HTreeBuilder: segment frontier synthesis failed for the required aligned lengths.";
    build_stage.skip({{"reason", "missing_required_segment_frontiers"}});
    return result;
  }

  auto exploration = htree_builder::ExploreDepthCandidates(result.topology, full_level_plans, depth_candidates, entry_sets_by_length,
                                                           segment_pattern_registry, base_resolved_options, char_builder.get_cap_lattice(),
                                                           result.char_slew_steps, options.target_depth.has_value());
  result.depth_candidate_count = exploration.depth_summaries.size();

  const auto covered_global_feasible_pool = htree_builder::FilterGlobalEntriesByActualBoundaryCoverage(
      exploration.global_feasible_pool, exploration.candidate_evaluations, result.topology, segment_pattern_registry,
      exploration.actual_load_legality_context);
  const auto covered_global_candidate_pool = htree_builder::FilterGlobalEntriesByActualBoundaryCoverage(
      exploration.global_candidate_pool, exploration.candidate_evaluations, result.topology, segment_pattern_registry,
      exploration.actual_load_legality_context);

  const auto selected_feasible_ref = htree_builder::SelectBestGlobalEntry(covered_global_feasible_pool.entries);
  const auto selected_fallback_ref = selected_feasible_ref.has_value()
                                         ? std::optional<htree_builder::CandidateCharRef>{}
                                         : htree_builder::SelectBestGlobalEntry(covered_global_candidate_pool.entries);
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_fallback_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    if (!covered_global_candidate_pool.first_failure_reason.empty()) {
      result.failure_reason = covered_global_candidate_pool.first_failure_reason;
    } else {
      result.failure_reason = exploration.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTreeBuilder: failed to select a best H-tree characterization entry across depth candidates.";
    build_stage.skip({{"reason", result.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  const auto selected_actual_legality = htree_builder::ResolveActualLoadLegality(
      result.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_registry, segment_pattern_registry,
      exploration.actual_load_legality_context);
  LOG_FATAL_IF(!selected_actual_legality.legal) << "HTreeBuilder: selected global frontier entry is missing actual-load legality coverage.";
  selected_summary.htree_load_group_count = selected_actual_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_actual_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_actual_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_actual_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_actual_legality.cap_distribution.cap_median_pf;

  result.selected_depth = selected_evaluation.depth;
  result.best_char = *selected_ref->entry;
  result.levels = selected_evaluation.levels;
  result.selected_final_frontier_count = selected_summary.final_frontier_count;
  result.selected_candidate_solution_count = selected_summary.candidate_solution_count;
  result.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
  result.selected_feasible_solution_count = selected_summary.feasible_solution_count;
  result.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
  result.min_top_input_slew_ns = selected_evaluation.resolved_options.min_top_input_slew_ns;
  result.top_input_slew_covering_idx = selected_evaluation.resolved_options.top_input_slew_covering_idx;
  result.htree_load_group_count = selected_summary.htree_load_group_count;
  result.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
  result.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
  result.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
  result.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

  if (!selected_feasible_ref.has_value()) {
    result.used_boundary_fallback = true;
    result.boundary_fallback_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.boundary_fallback_score
        = htree_builder::CalcBoundaryFallbackScore(*result.best_char, selected_evaluation.resolved_options, result.char_slew_steps);

    schema::EmitDiagnostic(
        schema::DiagnosticLevel::kFallback, "HTreeBuilder",
        "no depth candidate satisfied caller boundary constraints; selected fallback solution from the global candidate pool.",
        {
            {"reason", result.boundary_fallback_reason},
            {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
            {"fallback_score", std::to_string(result.boundary_fallback_score.value_or(0.0))},
            {"selected_top_input_slew_idx", std::to_string(result.best_char->get_input_slew_idx())},
            {"selected_leaf_load_cap_idx", std::to_string(result.best_char->get_leaf_load_cap_idx())},
        });
  }

  result.best_pattern = selected_evaluation.topology_pattern_registry.materialize(result.best_char->get_pattern_id());
  const auto& best_level_segment_pattern_ids = result.best_pattern->get_level_segment_pattern_ids();
  LOG_FATAL_IF(best_level_segment_pattern_ids.size() != result.levels.size())
      << "HTreeBuilder: best H-tree pattern level count does not match selected depth.";
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto segment_pattern_id = best_level_segment_pattern_ids.at(level_index);
    result.levels.at(level_index).segment_pattern_id = segment_pattern_id;
    const auto* segment_pattern = segment_pattern_registry.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: selected segment pattern metadata is missing.";
    result.levels.at(level_index).selected_has_any_buffer = !segment_pattern->get_cell_masters().empty();
    if (!segment_pattern->get_cell_masters().empty()) {
      result.levels.at(level_index).selected_leaf_buffer_cell_master = segment_pattern->get_cell_masters().back();
    } else {
      result.levels.at(level_index).selected_leaf_buffer_cell_master.clear();
    }
    result.levels.at(level_index).selected_has_terminal_branch_buffer = segment_pattern->hasTerminalBranchBuffer();
    if (segment_pattern->hasTerminalBranchBuffer() && !segment_pattern->get_cell_masters().empty()) {
      result.levels.at(level_index).selected_terminal_cell_master = segment_pattern->get_cell_masters().back();
    } else {
      result.levels.at(level_index).selected_terminal_cell_master.clear();
    }
  }
  const std::string selected_root_driver_cell_master = ResolveSelectedRootDriverCellMaster(result.levels);
  if (!htree_builder::ValidateRootDriverSizing(result, selected_root_driver_cell_master)) {
    result.failure_reason = "root_driver_sizing_precheck_failed";
    build_stage.skip({{"reason", result.failure_reason}});
    return result;
  }

  htree_builder::MaterializeCTSObjects(result, segment_pattern_registry);
  result.success = result.failure_reason.empty() && result.best_char.has_value() && result.best_pattern.has_value()
                   && result.root_output_pin != nullptr && result.root_net != nullptr;
  if (result.success) {
    LOG_FATAL_IF(!htree_builder::ApplyRootDriverSizing(result, selected_root_driver_cell_master))
        << "HTreeBuilder: prevalidated root-driver sizing failed during materialization.";
  }

  htree_builder::LogHTreeBuildSummary(result, selected_evaluation, selected_summary);
  build_stage.finish({{"success", result.success ? "true" : "false"},
                      {"levels", std::to_string(result.levels.size())},
                      {"inserted_insts", std::to_string(result.inserted_insts.size())},
                      {"inserted_nets", std::to_string(result.inserted_nets.size())},
                      {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers)}},
                     result.success ? "success" : "incomplete");
  return result;
}

}  // namespace icts
