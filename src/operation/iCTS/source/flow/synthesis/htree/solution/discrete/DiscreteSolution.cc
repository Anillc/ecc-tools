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
 * @file DiscreteSolution.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-26
 * @brief Discrete H-tree selected-solution search implementation.
 */

#include "synthesis/htree/solution/discrete/DiscreteSolution.hh"

#include <glog/logging.h>

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "characterization/Characterization.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/solution/report/StageReport.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::discrete_solution {

auto SelectDiscreteHTreeSolution(htree::DiagnosticBuild& result, const HTree::Config& config, SchemaWriter& reporter, unsigned max_depth,
                                 const std::vector<HTree::LevelPlan>& full_level_plans, const std::vector<unsigned>& depth_candidates,
                                 const htree::SegmentFrontierCatalog& segment_frontier_catalog,
                                 htree::BufferPatternLibrary& segment_pattern_library,
                                 const htree::BoundaryConstraints& search_boundary_constraints,
                                 const htree::HTreeFanoutPruningConfig& fanout_pruning_config,
                                 const htree::RootDriverCompensationInput& root_driver_compensation_input,
                                 const htree::SinkLoadRegionLegalityInput& sink_load_region_input, const CharBuilder& char_builder,
                                 const std::string& root_driver_clock_period_source) -> DiscreteHTreeSelectionBuild
{
  htree::DepthSearchBuild exploration;
  {
    auto depth_search_stage
        = reporter.beginStage("HTree", "Search topology depth candidates",
                              {
                                  {"depth_candidates", std::to_string(depth_candidates.size())},
                                  {"max_depth", std::to_string(max_depth)},
                                  {"segment_frontier_length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
                              },
                              htree::DetailStageReportOptions());
    exploration = htree::SearchTopologyDepthCandidates(
        result.output.topology, full_level_plans, depth_candidates, segment_frontier_catalog, segment_pattern_library,
        search_boundary_constraints, char_builder.get_cap_lattice(), result.diagnostics.char_slew_steps, config.target_depth.has_value(),
        root_driver_compensation_input, sink_load_region_input, reporter, fanout_pruning_config);
    depth_search_stage.finished({
        {"evaluated_depths", std::to_string(exploration.summary.depth_summaries.size())},
        {"global_feasible_refs", std::to_string(exploration.output.global_feasible_pool.size())},
        {"global_candidate_refs", std::to_string(exploration.output.global_candidate_pool.size())},
        {"compensated_candidates", std::to_string(exploration.summary.root_driver_compensation_stats.compensated_candidate_count)},
    });
  }
  result.diagnostics.depth_candidate_count = exploration.summary.depth_summaries.size();

  htree::CandidateCharRefFilterBuild covered_global_feasible_pool;
  htree::CandidateCharRefFilterBuild covered_global_candidate_pool;
  {
    auto coverage_stage
        = reporter.beginStage("HTree", "Filter global sink-load coverage",
                              {
                                  {"global_feasible_refs", std::to_string(exploration.output.global_feasible_pool.size())},
                                  {"global_candidate_refs", std::to_string(exploration.output.global_candidate_pool.size())},
                              },
                              htree::DetailStageReportOptions());
    covered_global_feasible_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.output.global_feasible_pool, exploration.output.candidate_evaluations, result.output.topology, segment_pattern_library,
        exploration.output.sink_load_region_legality_context);
    covered_global_candidate_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.output.global_candidate_pool, exploration.output.candidate_evaluations, result.output.topology, segment_pattern_library,
        exploration.output.sink_load_region_legality_context);
    coverage_stage.finished({
        {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.output.entries.size())},
        {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.output.entries.size())},
        {"first_feasible_failure", covered_global_feasible_pool.summary.first_failure_reason.empty()
                                       ? "none"
                                       : covered_global_feasible_pool.summary.first_failure_reason},
        {"first_candidate_failure", covered_global_candidate_pool.summary.first_failure_reason.empty()
                                        ? "none"
                                        : covered_global_candidate_pool.summary.first_failure_reason},
    });
  }

  std::vector<htree::CandidateCharRef> per_depth_feasible_pareto_pool;
  std::optional<htree::CandidateCharRef> selected_feasible_ref;
  std::optional<htree::CandidateCharRef> selected_relaxed_ref;
  {
    auto selection_stage
        = reporter.beginStage("HTree", "Select global topology",
                              {
                                  {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.output.entries.size())},
                                  {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.output.entries.size())},
                              },
                              htree::DetailStageReportOptions());
    per_depth_feasible_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_feasible_pool.output.entries);
    selected_feasible_ref = htree::SelectBestGlobalEntry(per_depth_feasible_pareto_pool);
    std::size_t per_depth_candidate_pareto_count = 0U;
    if (!selected_feasible_ref.has_value() && config.allow_boundary_relaxation) {
      const auto per_depth_candidate_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_candidate_pool.output.entries);
      per_depth_candidate_pareto_count = per_depth_candidate_pareto_pool.size();
      selected_relaxed_ref = htree::SelectBestGlobalEntry(per_depth_candidate_pareto_pool);
    }
    std::string selected_from = "none";
    if (selected_feasible_ref.has_value()) {
      selected_from = "strict_feasible";
    } else if (selected_relaxed_ref.has_value()) {
      selected_from = "relaxed_boundary";
    }
    selection_stage.finished({
        {"feasible_pareto_refs", std::to_string(per_depth_feasible_pareto_pool.size())},
        {"candidate_pareto_refs", std::to_string(per_depth_candidate_pareto_count)},
        {"selected_from", selected_from},
    });
  }
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_relaxed_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    DiscreteHTreeSelectionBuild selection_build;
    if (!selected_feasible_ref.has_value() && !config.allow_boundary_relaxation) {
      selection_build.failure_reason = "no_strict_boundary_feasible_solution_any_depth";
    } else if (!covered_global_candidate_pool.summary.first_failure_reason.empty()) {
      selection_build.failure_reason = covered_global_candidate_pool.summary.first_failure_reason;
    } else {
      selection_build.failure_reason = exploration.output.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTree: failed to select a strict-feasible H-tree characterization entry across depth candidates.";
    return selection_build;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.output.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.summary.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  htree::EmitDepthCandidateSummary(reporter, exploration.summary.depth_summaries);
  htree::SinkLoadRegionLegalitySummary selected_sink_load_region_legality;
  {
    auto selected_legality_stage
        = reporter.beginStage("HTree", "Resolve selected sink-load legality",
                              {
                                  {"selected_depth", std::to_string(selected_evaluation.depth)},
                                  {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                              },
                              htree::DetailStageReportOptions());
    selected_sink_load_region_legality = htree::ResolveSinkLoadRegionLegality(
        result.output.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library,
        segment_pattern_library, exploration.output.sink_load_region_legality_context);
    selected_legality_stage.finished({
        {"legal", selected_sink_load_region_legality.legal ? "true" : "false"},
        {"required_leaf_load_cap_idx", selected_sink_load_region_legality.required_leaf_load_cap_covering_idx.has_value()
                                           ? std::to_string(*selected_sink_load_region_legality.required_leaf_load_cap_covering_idx)
                                           : "none"},
        {"failure_reason",
         selected_sink_load_region_legality.failure_reason.empty() ? "none" : selected_sink_load_region_legality.failure_reason},
    });
  }
  if (!selected_sink_load_region_legality.legal) {
    LOG_WARNING << "HTree: selected global frontier entry is missing sink-load-region legality coverage.";
    return DiscreteHTreeSelectionBuild{
        .selected = false,
        .failure_reason = "sink_load_region_legality_missing",
        .selected_solution = {},
    };
  }
  selected_summary.htree_load_group_count = selected_sink_load_region_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_sink_load_region_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_sink_load_region_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_sink_load_region_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_sink_load_region_legality.cap_distribution.cap_median_pf;
  selected_evaluation.best_char = *selected_ref->entry;

  htree::RootDriverCompensationPass selected_compensation_pass(root_driver_compensation_input);
  htree::RootDriverCompensationDetail selected_compensation_detail;
  {
    auto selected_compensation_stage
        = reporter.beginStage("HTree", "Resolve selected root-driver compensation",
                              {
                                  {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                                  {"root_driver_sizing_enabled", config.enable_root_driver_sizing ? "true" : "false"},
                              },
                              htree::DetailStageReportOptions());
    selected_compensation_detail
        = selected_compensation_pass.evaluate(selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library,
                                              segment_pattern_library, result.output.topology);
    selected_compensation_stage.finished({
        {"valid", selected_compensation_detail.valid ? "true" : "false"},
        {"cell_master", selected_compensation_detail.cell_master.empty() ? "none" : selected_compensation_detail.cell_master},
        {"load_cap_pf", std::to_string(selected_compensation_detail.load_cap_pf)},
    });
  }

  const bool used_boundary_relaxation = !selected_feasible_ref.has_value();
  const std::string boundary_relaxation_reason = used_boundary_relaxation ? "no_strict_boundary_feasible_solution_any_depth" : "";
  const std::optional<double> boundary_relaxation_score
      = used_boundary_relaxation
            ? std::optional<double>(htree::CalcBoundaryRelaxationScore(
                  *selected_evaluation.best_char, selected_evaluation.boundary_constraints, result.diagnostics.char_slew_steps))
            : std::nullopt;

  DiscreteHTreeSelectionBuild selection_build;
  selection_build.selected = true;
  selection_build.selected_solution = htree::HTreeSelectedSolution{
      .engine = htree::HTreeSelectionEngine::kDiscrete,
      .evaluation = selected_evaluation,
      .summary = selected_summary,
      .compensation_stats = exploration.summary.root_driver_compensation_stats,
      .compensation_detail = selected_compensation_detail,
      .root_driver_clock_period_source = root_driver_clock_period_source,
      .used_boundary_relaxation = used_boundary_relaxation,
      .boundary_relaxation_reason = boundary_relaxation_reason,
      .boundary_relaxation_score = boundary_relaxation_score,
  };
  return selection_build;
}

}  // namespace icts::htree::discrete_solution
