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
 * @file AnalyticalSolution.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree build-stage orchestration
 */

#include "synthesis/htree/solution/analytical/AnalyticalSolution.hh"

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Log.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/selection/AnalyticalSelection.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/solution/report/SolutionReport.hh"
#include "synthesis/htree/solution/report/StageReport.hh"
#include "synthesis/htree/solution/selection/SolutionSelection.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::analytical_solution {
namespace as = analytical_selection;

auto TryBuildAnalyticalHTree(htree::DiagnosticBuild& result, const HTree::Input& input, const HTree::Config& config,
                             SchemaWriter::StageScope& build_stage, unsigned max_depth,
                             const std::vector<HTree::LevelPlan>& full_level_plans, const std::vector<unsigned>& depth_candidates,
                             const htree::SegmentFrontierCatalog& segment_frontier_catalog,
                             htree::BufferPatternLibrary& segment_pattern_library,
                             const htree::BoundaryConstraints& search_boundary_constraints,
                             const htree::HTreeFanoutPruningConfig& fanout_pruning_config,
                             const htree::RootDriverCompensationInput& root_driver_compensation_input,
                             const htree::SinkLoadRegionLegalityInput& sink_load_region_input, const CharBuilder& char_builder,
                             const std::string& root_driver_clock_period_source) -> bool
{
  if (!config.enable_analytical_solver) {
    return false;
  }
  LOG_FATAL_IF(input.design == nullptr) << "HTree analytical solution requires explicit Design dependency.";
  LOG_FATAL_IF(input.sta_adapter == nullptr) << "HTree analytical solution requires explicit STAAdapter dependency.";
  LOG_FATAL_IF(input.reporter == nullptr) << "HTree analytical solution requires explicit reporter dependency.";
  auto& design = *input.design;
  auto& sta_adapter = *input.sta_adapter;
  auto& reporter = *input.reporter;
  auto analytical_stage = reporter.beginStage("HTree", "Try analytical topology candidates",
                                              {
                                                  {"depth_candidates", std::to_string(depth_candidates.size())},
                                                  {"max_depth", std::to_string(max_depth)},
                                                  {"per_level_shortlist", std::to_string(as::kAnalyticalPerLevelShortlistSize)},
                                                  {"top_k_per_depth", std::to_string(as::kAnalyticalTopKPerDepth)},
                                              },
                                              DetailStageReportOptions());
  const auto analytical_attempt = as::TrySolveAnalyticalHTree(
      result.output.topology, full_level_plans, depth_candidates, segment_frontier_catalog, segment_pattern_library,
      search_boundary_constraints, fanout_pruning_config, root_driver_compensation_input, sink_load_region_input, char_builder,
      result.diagnostics.char_slew_steps);
  result.diagnostics.analytical_model_set_count = analytical_attempt.model_set_count;
  result.diagnostics.analytical_rejected_fit_count = analytical_attempt.rejected_fit_count;
  result.diagnostics.analytical_structural_cap_operator_count = analytical_attempt.structural_cap_operator_count;
  result.diagnostics.analytical_evaluated_segment_count = analytical_attempt.evaluated_segment_count;
  result.diagnostics.analytical_generated_candidate_count = analytical_attempt.generated_candidate_count;
  result.diagnostics.analytical_validated_candidate_count = analytical_attempt.validated_candidate_count;
  result.diagnostics.analytical_validated_pareto_count = analytical_attempt.validated_pareto_count;
  result.diagnostics.analytical_selected_pareto_power_rank = analytical_attempt.selected_pareto_power_rank;
  result.diagnostics.analytical_validated_delay_min_ns = analytical_attempt.validated_delay_min_ns;
  result.diagnostics.analytical_validated_delay_median_ns = analytical_attempt.validated_delay_median_ns;
  result.diagnostics.analytical_validated_delay_max_ns = analytical_attempt.validated_delay_max_ns;
  result.diagnostics.analytical_validated_power_min_w = analytical_attempt.validated_power_min_w;
  result.diagnostics.analytical_validated_power_median_w = analytical_attempt.validated_power_median_w;
  result.diagnostics.analytical_validated_power_max_w = analytical_attempt.validated_power_max_w;

  if (analytical_attempt.selected && analytical_attempt.selected_evaluation.best_char.has_value()) {
    result.diagnostics.analytical_mode_selected = true;
    analytical_stage.finished({
        {"selected_depth", std::to_string(analytical_attempt.selected_evaluation.depth)},
        {"model_sets", std::to_string(analytical_attempt.model_set_count)},
        {"rejected_fits", std::to_string(analytical_attempt.rejected_fit_count)},
        {"evaluated_segments", std::to_string(analytical_attempt.evaluated_segment_count)},
        {"scored_segments", std::to_string(analytical_attempt.scored_segment_count)},
        {"missing_models", std::to_string(analytical_attempt.missing_model_count)},
        {"decomposition_rejections", std::to_string(analytical_attempt.decomposition_rejected_count)},
        {"metric_rejections", std::to_string(analytical_attempt.metric_evaluation_rejected_count)},
        {"domain_slew_rejections", std::to_string(analytical_attempt.domain_slew_rejected_count)},
        {"domain_cap_rejections", std::to_string(analytical_attempt.domain_cap_rejected_count)},
        {"domain_slew_floors", std::to_string(analytical_attempt.domain_slew_floor_count)},
        {"domain_cap_floors", std::to_string(analytical_attempt.domain_cap_floor_count)},
        {"max_domain_rejected_cap_pf", std::to_string(analytical_attempt.max_domain_rejected_cap_pf)},
        {"empty_shortlists", std::to_string(analytical_attempt.empty_shortlist_count)},
        {"materialization_attempts", std::to_string(analytical_attempt.materialization_attempt_count)},
        {"root_fanout_rejections", std::to_string(analytical_attempt.root_fanout_rejected_count)},
        {"lattice_rejections", std::to_string(analytical_attempt.lattice_rejected_count)},
        {"diagnostic_library_hits", std::to_string(analytical_attempt.diagnostic_library_hit_count)},
        {"diagnostic_frontier_hits", std::to_string(analytical_attempt.diagnostic_frontier_hit_count)},
        {"diagnostic_decomposed", std::to_string(analytical_attempt.diagnostic_decomposed_count)},
        {"diagnostic_scored", std::to_string(analytical_attempt.diagnostic_scored_count)},
        {"diagnostic_shortlisted", std::to_string(analytical_attempt.diagnostic_shortlisted_count)},
        {"diagnostic_generated_candidates", std::to_string(analytical_attempt.diagnostic_generated_candidate_count)},
        {"diagnostic_direct_candidates", std::to_string(analytical_attempt.diagnostic_direct_candidate_count)},
        {"diagnostic_direct_delay_ns", std::to_string(analytical_attempt.diagnostic_direct_delay_ns)},
        {"diagnostic_direct_power_w", std::to_string(analytical_attempt.diagnostic_direct_power_w)},
        {"diagnostic_direct_root_cap_pf", std::to_string(analytical_attempt.diagnostic_direct_root_cap_pf)},
        {"diagnostic_direct_input_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_input_slew_idx)},
        {"diagnostic_direct_output_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_output_slew_idx)},
        {"diagnostic_direct_driven_cap_idx", std::to_string(analytical_attempt.diagnostic_direct_driven_cap_idx)},
        {"generated_candidates", std::to_string(analytical_attempt.generated_candidate_count)},
        {"validated_candidates", std::to_string(analytical_attempt.validated_candidate_count)},
        {"validated_pareto_candidates", std::to_string(analytical_attempt.validated_pareto_count)},
        {"selected_pareto_power_rank", std::to_string(analytical_attempt.selected_pareto_power_rank)},
        {"validated_delay_min_ns", std::to_string(analytical_attempt.validated_delay_min_ns)},
        {"validated_delay_median_ns", std::to_string(analytical_attempt.validated_delay_median_ns)},
        {"validated_delay_max_ns", std::to_string(analytical_attempt.validated_delay_max_ns)},
        {"validated_power_min_w", std::to_string(analytical_attempt.validated_power_min_w)},
        {"validated_power_median_w", std::to_string(analytical_attempt.validated_power_median_w)},
        {"validated_power_max_w", std::to_string(analytical_attempt.validated_power_max_w)},
    });

    auto selected_evaluation = analytical_attempt.selected_evaluation;
    auto selected_summary = analytical_attempt.selected_summary;
    result.diagnostics.depth_candidate_count = depth_candidates.size();
    result.summary.selected_depth = selected_evaluation.depth;
    result.output.best_char = *selected_evaluation.best_char;

    htree::DepthSearchBuild analytical_exploration;
    as::ApplyAnalyticalRootDriverStats(analytical_exploration, analytical_attempt, root_driver_compensation_input);
    ApplyRootDriverCompensationSummary(result, analytical_exploration, analytical_attempt.selected_compensation_detail,
                                       *result.output.best_char);
    result.diagnostics.root_driver_compensation.clock_period_source = root_driver_clock_period_source;
    result.output.levels = selected_evaluation.levels;
    result.diagnostics.selected_final_frontier_count = selected_summary.final_frontier_count;
    result.diagnostics.selected_candidate_solution_count = selected_summary.candidate_solution_count;
    result.diagnostics.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
    result.diagnostics.selected_feasible_solution_count = selected_summary.feasible_solution_count;
    result.diagnostics.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
    result.diagnostics.min_top_input_slew_ns = selected_evaluation.boundary_constraints.min_top_input_slew_ns;
    result.diagnostics.top_input_slew_covering_idx = selected_evaluation.boundary_constraints.top_input_slew_covering_idx;
    result.diagnostics.htree_load_group_count = selected_summary.htree_load_group_count;
    result.diagnostics.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
    result.diagnostics.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
    result.diagnostics.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
    result.diagnostics.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

    result.output.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.output.best_char->get_pattern_id());
    ApplySelectedPatternToLevelPlans(sta_adapter, result, segment_pattern_library);
    const std::string selected_root_driver_cell_master = ResolveSelectedRootDriverCellMaster(result.output.levels);
    if (config.enable_root_driver_sizing
        && !htree::ValidateRootDriverSizing(design, sta_adapter, result, selected_root_driver_cell_master)) {
      result.summary.failure_reason = "root_driver_sizing_precheck_failed";
      build_stage.failed({{"reason", result.summary.failure_reason}});
      return true;
    }

    {
      auto embedding_stage = reporter.beginStage("HTree", "Build selected embedding",
                                                 {
                                                     {"selected_depth", std::to_string(result.summary.selected_depth.value_or(0U))},
                                                     {"selected_levels", std::to_string(result.output.levels.size())},
                                                     {"selection_engine", "analytical"},
                                                 },
                                                 DetailStageReportOptions());
      htree::BuildEmbedding(design, sta_adapter, result, segment_pattern_library);
      result.summary.success = result.summary.failure_reason.empty() && result.output.best_char.has_value()
                               && result.output.best_pattern.has_value() && result.output.root_output_pin != nullptr
                               && result.output.root_net != nullptr;
      if (result.summary.success && config.enable_root_driver_sizing) {
        LOG_FATAL_IF(!htree::ApplyRootDriverSizing(design, sta_adapter, result, selected_root_driver_cell_master))
            << "HTree: prevalidated root-driver sizing failed during analytical embedding construction.";
      } else if (result.summary.success && result.output.root_inst != nullptr) {
        result.diagnostics.selected_root_driver_cell_master = result.output.root_inst->get_cell_master();
      }
      if (result.summary.success) {
        embedding_stage.finished({
            {"inserted_insts", std::to_string(result.output.inserted_insts.size())},
            {"inserted_nets", std::to_string(result.output.inserted_nets.size())},
            {"pruned_leaf_single_load_buffers", std::to_string(result.diagnostics.pruned_leaf_single_load_buffers)},
        });
      } else {
        embedding_stage.failed(
            {{"reason", result.summary.failure_reason.empty() ? "incomplete_embedding_build" : result.summary.failure_reason}});
      }
    }

    {
      auto summary_stage = reporter.beginStage("HTree", "Emit synthesis summary", {}, StageReportOptions{.emit_success_summary = false});
      htree::LogSynthesisSummary(reporter, result, selected_evaluation, selected_summary);
      summary_stage.finished();
    }
    if (result.summary.success) {
      build_stage.finished({{"selection_engine", "analytical"}});
    } else {
      build_stage.failed({{"reason", result.summary.failure_reason.empty() ? "incomplete_embedding_build" : result.summary.failure_reason},
                          {"selection_engine", "analytical"}});
    }
    return true;
  }

  result.diagnostics.analytical_failure_reason
      = analytical_attempt.failure_reason.empty() ? "analytical_candidate_unavailable" : analytical_attempt.failure_reason;
  analytical_stage.failed({
      {"selected_depth", "none"},
      {"reason", result.diagnostics.analytical_failure_reason},
      {"model_sets", std::to_string(analytical_attempt.model_set_count)},
      {"rejected_fits", std::to_string(analytical_attempt.rejected_fit_count)},
      {"evaluated_segments", std::to_string(analytical_attempt.evaluated_segment_count)},
      {"scored_segments", std::to_string(analytical_attempt.scored_segment_count)},
      {"missing_models", std::to_string(analytical_attempt.missing_model_count)},
      {"decomposition_rejections", std::to_string(analytical_attempt.decomposition_rejected_count)},
      {"metric_rejections", std::to_string(analytical_attempt.metric_evaluation_rejected_count)},
      {"domain_slew_rejections", std::to_string(analytical_attempt.domain_slew_rejected_count)},
      {"domain_cap_rejections", std::to_string(analytical_attempt.domain_cap_rejected_count)},
      {"domain_slew_floors", std::to_string(analytical_attempt.domain_slew_floor_count)},
      {"domain_cap_floors", std::to_string(analytical_attempt.domain_cap_floor_count)},
      {"max_domain_rejected_cap_pf", std::to_string(analytical_attempt.max_domain_rejected_cap_pf)},
      {"empty_shortlists", std::to_string(analytical_attempt.empty_shortlist_count)},
      {"materialization_attempts", std::to_string(analytical_attempt.materialization_attempt_count)},
      {"root_fanout_rejections", std::to_string(analytical_attempt.root_fanout_rejected_count)},
      {"lattice_rejections", std::to_string(analytical_attempt.lattice_rejected_count)},
      {"diagnostic_library_hits", std::to_string(analytical_attempt.diagnostic_library_hit_count)},
      {"diagnostic_frontier_hits", std::to_string(analytical_attempt.diagnostic_frontier_hit_count)},
      {"diagnostic_decomposed", std::to_string(analytical_attempt.diagnostic_decomposed_count)},
      {"diagnostic_scored", std::to_string(analytical_attempt.diagnostic_scored_count)},
      {"diagnostic_shortlisted", std::to_string(analytical_attempt.diagnostic_shortlisted_count)},
      {"diagnostic_generated_candidates", std::to_string(analytical_attempt.diagnostic_generated_candidate_count)},
      {"diagnostic_direct_candidates", std::to_string(analytical_attempt.diagnostic_direct_candidate_count)},
      {"diagnostic_direct_delay_ns", std::to_string(analytical_attempt.diagnostic_direct_delay_ns)},
      {"diagnostic_direct_power_w", std::to_string(analytical_attempt.diagnostic_direct_power_w)},
      {"diagnostic_direct_root_cap_pf", std::to_string(analytical_attempt.diagnostic_direct_root_cap_pf)},
      {"diagnostic_direct_input_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_input_slew_idx)},
      {"diagnostic_direct_output_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_output_slew_idx)},
      {"diagnostic_direct_driven_cap_idx", std::to_string(analytical_attempt.diagnostic_direct_driven_cap_idx)},
      {"first_empty_level",
       analytical_attempt.first_empty_reason.empty() ? "none" : std::to_string(analytical_attempt.first_empty_level_index)},
      {"first_empty_length_idx",
       analytical_attempt.first_empty_reason.empty() ? "none" : std::to_string(analytical_attempt.first_empty_length_idx)},
      {"first_empty_reason", analytical_attempt.first_empty_reason.empty() ? "none" : analytical_attempt.first_empty_reason},
      {"generated_candidates", std::to_string(analytical_attempt.generated_candidate_count)},
      {"validated_candidates", std::to_string(analytical_attempt.validated_candidate_count)},
      {"validated_pareto_candidates", std::to_string(analytical_attempt.validated_pareto_count)},
      {"selected_pareto_power_rank", std::to_string(analytical_attempt.selected_pareto_power_rank)},
      {"validated_delay_min_ns", std::to_string(analytical_attempt.validated_delay_min_ns)},
      {"validated_delay_median_ns", std::to_string(analytical_attempt.validated_delay_median_ns)},
      {"validated_delay_max_ns", std::to_string(analytical_attempt.validated_delay_max_ns)},
      {"validated_power_min_w", std::to_string(analytical_attempt.validated_power_min_w)},
      {"validated_power_median_w", std::to_string(analytical_attempt.validated_power_median_w)},
      {"validated_power_max_w", std::to_string(analytical_attempt.validated_power_max_w)},
  });
  EmitDiagnostic(reporter, DiagnosticLevel::kError, "HTree", "analytical H-tree candidate selection did not produce a validated candidate.",
                 {
                     {"reason", result.diagnostics.analytical_failure_reason},
                     {"model_sets", std::to_string(result.diagnostics.analytical_model_set_count)},
                     {"generated_candidates", std::to_string(result.diagnostics.analytical_generated_candidate_count)},
                     {"validated_candidates", std::to_string(result.diagnostics.analytical_validated_candidate_count)},
                 });
  result.summary.failure_reason = result.diagnostics.analytical_failure_reason;
  build_stage.failed({{"reason", result.summary.failure_reason}, {"selection_engine", "analytical"}});
  return true;
}

}  // namespace icts::htree::analytical_solution
