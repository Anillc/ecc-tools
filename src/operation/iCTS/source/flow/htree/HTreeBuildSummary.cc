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
 * @file HTreeBuildSummary.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree build-summary report assembly.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Log.hh"
#include "LogFormat.hh"
#include "PatternId.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"
#include "logger/Schema.hh"

namespace icts::htree_builder {

auto LogHTreeBuildSummary(const HTreeBuilder::BuildResult& result, const CandidateBuildEvaluation& selected_evaluation,
                          const HTreeDepthCandidateSummary& selected_summary) -> void
{
  const bool selected_has_boundary_constraints = HasBoundaryConstraints(selected_evaluation.resolved_options);
  if (!result.best_char.has_value()) {
    LOG_WARNING << "HTreeBuilder: build summary skipped because no selected topology char is available.";
    return;
  }
  const auto& best_char = *result.best_char;
  const auto selected_terminal_branch_buffered_levels = static_cast<std::size_t>(std::ranges::count_if(
      result.levels, [](const HTreeBuilder::LevelPlan& level) -> bool { return level.selected_has_terminal_branch_buffer; }));

  SCHEMA_WRITER_INST.emitSection("### H-Tree Selection");
  logformat::TableRows build_summary_rows = {
      {"clock_name", result.log_context.clock_name.empty() ? "n/a" : result.log_context.clock_name,
       "context for repeated H-tree/top-level sections"},
      {"clock_net_name", result.log_context.clock_net_name.empty() ? "n/a" : result.log_context.clock_net_name,
       "context for repeated H-tree/top-level sections"},
      {"sink_domain", result.log_context.sink_domain.empty() ? "n/a" : result.log_context.sink_domain,
       "context for repeated H-tree/top-level sections"},
      {"stage", result.log_context.stage.empty() ? "n/a" : result.log_context.stage, "context for repeated H-tree/top-level sections"},
      {"object_name_prefix", result.object_name_prefix.empty() ? "n/a" : result.object_name_prefix, "context for inserted object names"},
      {"levels", std::to_string(result.levels.size()), "selected H-tree levels"},
      {"depth_candidates", std::to_string(result.depth_candidate_count), "evaluated descending depth candidates"},
      {"selected_depth", result.selected_depth.has_value() ? std::to_string(*result.selected_depth) : "none",
       "global winner across all evaluated depth candidates"},
      {"selected_topology_pattern_id", std::to_string(best_char.get_pattern_id().local_id),
       result.used_boundary_fallback ? "selected fallback topology pattern from candidate frontier selection entries"
                                     : "selected strict-feasible topology pattern from the global feasible frontier pool"},
      {"selection_policy", result.used_boundary_fallback ? "global_boundary_fallback" : "global_frontier_pareto_power_median",
       result.used_boundary_fallback
           ? "the global strict-feasible pool across all depth candidates is empty; fallback selection uses the global candidate "
             "frontier pool with delay-power Pareto power-median ordering"
           : "the global feasible frontier pool is Pareto filtered and the lower power-ordered median entry is selected"},
      {"final_frontier_count", std::to_string(selected_summary.final_frontier_count),
       "selected-depth root frontier size before boundary filtering and actual-load legality filtering"},
      {"candidate_solutions", std::to_string(selected_summary.candidate_solution_count),
       selected_has_boundary_constraints ? "selected-depth frontier entries after full composition"
                                         : "not materialized on unrestricted builds"},
      {"candidate_frontier_entry_count", std::to_string(selected_summary.candidate_frontier_entry_count),
       selected_has_boundary_constraints ? "selected-depth actual-load-legal candidate frontier entries before feasible filtering"
                                         : "not materialized on unrestricted builds"},
      {"feasible_solutions", std::to_string(selected_summary.feasible_solution_count),
       selected_has_boundary_constraints ? "selected-depth strict-feasible entries after boundary filtering" : "same as composed frontier"},
      {"feasible_frontier_entry_count", std::to_string(selected_summary.feasible_frontier_entry_count),
       "selected-depth actual-load-legal frontier entries after feasible filtering"},
      {"inserted_insts", std::to_string(result.inserted_insts.size()), "materialized CTS buffer instances"},
      {"inserted_nets", std::to_string(result.inserted_nets.size()), "materialized CTS nets"},
      {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers),
       "post-materialization redundant leaf buffers removed when a leaf buffer directly drove one external load"},
      {"power", logformat::FormatPowerW(best_char.get_power()), "selected pattern metric (total power)"},
      {"delay", logformat::FormatWithUnit(best_char.get_delay(), "ns"), "selected pattern metric"},
      {"root_driven_cap_idx", std::to_string(best_char.get_driven_cap_idx()), "selected pattern metric"},
      {"leaf_load_cap_idx", std::to_string(best_char.get_leaf_load_cap_idx()), "selected pattern metric"},
      {"leaf_output_slew_idx", std::to_string(best_char.get_output_slew_idx()), "selected pattern metric"},
      {"root_load_cap_idx", std::to_string(best_char.get_load_cap_idx()), "selected pattern metric"},
      {"selected_terminal_branch_buffered_levels",
       std::to_string(selected_terminal_branch_buffered_levels) + "/" + std::to_string(result.levels.size()),
       "actual selected H-tree levels whose segment pattern includes a terminal branch buffer"},
      {"top_input_slew_covering_idx",
       selected_evaluation.resolved_options.top_input_slew_covering_idx.has_value()
           ? std::to_string(*selected_evaluation.resolved_options.top_input_slew_covering_idx)
           : "none",
       selected_evaluation.resolved_options.min_top_input_slew_ns.has_value()
           ? logformat::FormatWithUnit(*selected_evaluation.resolved_options.min_top_input_slew_ns, "ns")
           : "unconstrained"},
      {"htree_load_group_count", std::to_string(selected_summary.htree_load_group_count),
       "selected H-tree external-load groups driven by the bottom-most buffered segments"},
      {"htree_load_cap_min", logformat::FormatWithUnit(selected_summary.htree_load_cap_min_pf, "pF"),
       "selected H-tree external-load total-cap minimum across real driven-load groups"},
      {"htree_load_cap_max", logformat::FormatWithUnit(selected_summary.htree_load_cap_max_pf, "pF"),
       "selected H-tree external-load total-cap maximum across real driven-load groups"},
      {"htree_load_cap_mean", logformat::FormatWithUnit(selected_summary.htree_load_cap_mean_pf, "pF"),
       "selected H-tree external-load total-cap mean across real driven-load groups"},
      {"htree_load_cap_median", logformat::FormatWithUnit(selected_summary.htree_load_cap_median_pf, "pF"),
       "selected H-tree external-load total-cap median across real driven-load groups"},
      {"selected_root_driver_cell_master",
       result.selected_root_driver_cell_master.empty() ? "none" : result.selected_root_driver_cell_master,
       "root driver master applied to the input root-net driver inst"},
      {"root_driver_sizing_enabled", logformat::FormatBool(result.root_driver_sizing_enabled),
       result.root_driver_sizing_enabled ? "root driver may be resized when the root is a CTS-owned buffer"
                                         : "root driver sizing is disabled for immutable top-level clock source"},
      {"used_boundary_fallback", logformat::FormatBool(result.used_boundary_fallback),
       result.used_boundary_fallback ? result.boundary_fallback_reason : "constraints satisfied without fallback"},
      {"boundary_fallback_score", result.boundary_fallback_score.has_value() ? std::to_string(*result.boundary_fallback_score) : "none",
       result.used_boundary_fallback ? "diagnostic normalized active-boundary score of the selected fallback" : "not used"},
  };
  {
    const auto is_duplicate_frontier_row = [&](const auto& row) -> bool {
      if (row.empty()) {
        return false;
      }
      const auto& field = row.front();
      if (!selected_has_boundary_constraints
          && (field == "candidate_solutions" || field == "candidate_frontier_entry_count" || field == "feasible_frontier_entry_count")) {
        return true;
      }
      if (!selected_has_boundary_constraints && field == "feasible_solutions"
          && selected_summary.feasible_solution_count == selected_summary.final_frontier_count) {
        return true;
      }
      if (field == "candidate_solutions" && selected_summary.candidate_solution_count == selected_summary.final_frontier_count) {
        return true;
      }
      if (field == "candidate_frontier_entry_count"
          && selected_summary.candidate_frontier_entry_count == selected_summary.candidate_solution_count) {
        return true;
      }
      if (field == "feasible_frontier_entry_count"
          && selected_summary.feasible_frontier_entry_count == selected_summary.feasible_solution_count) {
        return true;
      }
      return false;
    };
    auto duplicate_rows = std::ranges::remove_if(build_summary_rows, is_duplicate_frontier_row);
    build_summary_rows.erase(duplicate_rows.begin(), duplicate_rows.end());
  }
  LogInfoTable("HTreeBuilder Build Summary", {"Item", "Value", "Detail"}, build_summary_rows);
}

}  // namespace icts::htree_builder
