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
 * @file HTree.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief H-tree topology-family synthesis entry implementation.
 */

#include "synthesis/htree/HTree.hh"

#include <glog/logging.h>
#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "TopologyConfig.hh"
#include "TopologyGen.hh"
#include "Tree.hh"
#include "characterization/Characterization.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/characterization/Characterization.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/plan/Plan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/solution/analytical/AnalyticalSolution.hh"
#include "synthesis/htree/solution/report/SolutionReport.hh"
#include "synthesis/htree/solution/report/StageReport.hh"
#include "synthesis/htree/solution/selection/SolutionSelection.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts {

auto HTree::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto HTree::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  result.log_context = options.log_context;
  result.object_name_prefix = options.object_name_prefix;
  result.root_net = &root_net;
  result.root_output_pin = root_net.get_driver();
  result.root_inst = result.root_output_pin == nullptr ? nullptr : result.root_output_pin->get_inst();
  if (result.root_output_pin == nullptr) {
    result.failure_reason = "missing_root_driver_pin";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no driver pin.";
    return result;
  }

  const auto loads = root_net.get_loads();
  if (loads.empty()) {
    result.failure_reason = "empty_root_net_loads";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no loads.";
    return result;
  }
  auto build_stage = SCHEMA_WRITER_INST.beginStage("HTree", "build", {}, schema::StageReportOptions{.emit_success_summary = false});
  if (loads.size() == 1U) {
    const auto root = result.topology.create_node();
    result.topology.set_root(root);
    if (auto* root_node = result.topology.get_node(root); root_node != nullptr) {
      root_node->get_position() = options.fixed_topology_root_location.value_or(loads.front()->get_location());
    }
    result.selected_depth = 0U;
    result.root_driver_sizing_enabled = options.enable_root_driver_sizing;
    result.target_depth = options.target_depth;
    result.success = true;
    build_stage.finished({
        {"reason", "trivial_single_load"},
        {"selected_depth", "0"},
        {"inserted_insts", "0"},
        {"inserted_nets", "0"},
    });
    return result;
  }

  const int32_t dbu_per_um = WRAPPER_INST.queryDbUnit();
  LOG_FATAL_IF(dbu_per_um <= 0) << "HTree: build failed because DBU-per-micron is unavailable.";

  BiPartitionConfig topology_config;
  topology_config.htree_topology_tolerance
      = std::max(0.0, options.htree_topology_tolerance.value_or(CONFIG_INST.get_htree_topology_tolerance()));
  topology_config.max_leaf_load_count = CONFIG_INST.get_max_fanout();
  result.topology
      = TopologyGen::build(loads, TopologyGen::BuildOptions{
                                      .partition_config = topology_config,
                                      .target_depth = std::nullopt,
                                      .fixed_root_location = options.fixed_topology_root_location,
                                      .dbu_per_um = dbu_per_um,
                                      .load_count_kind = options.topology_loads_are_local_buffers ? TopologyGen::LoadCountKind::kLocalBuffer
                                                                                                  : TopologyGen::LoadCountKind::kSink,
                                      .clock_name = options.log_context.clock_name,
                                      .clock_net_name = options.log_context.clock_net_name,
                                      .sink_domain = options.log_context.sink_domain,
                                      .stage = options.log_context.stage,
                                  });
  const auto levels = result.topology.levels();
  if (levels.size() <= 1U) {
    LOG_WARNING << "HTree: topology has no H-tree levels after generation.";
    build_stage.skip({{"reason", "no_h_tree_levels"}});
    return result;
  }

  build_stage.markRunning("characterization");
  CharacterizationLibrary local_char_library;
  auto* char_library = options.characterization_library == nullptr ? &local_char_library : options.characterization_library;
  const auto char_options = CharacterizationLibrary::buildRuntimeOptions();
  const auto char_flow = htree::RunCharacterizationFlow(result.topology, dbu_per_um, char_options, result, *char_library, options);
  if (!char_flow.success) {
    build_stage.failed({{"reason", char_flow.failure_reason}});
    return result;
  }
  const auto& char_builder = char_library->getCharBuilder();

  const auto base_boundary_constraints = htree::ResolveBoundaryConstraints(options, char_builder);
  result.force_branch_buffer = base_boundary_constraints.force_branch_buffer;
  result.root_driver_sizing_enabled = options.enable_root_driver_sizing;
  result.target_depth = options.target_depth;
  const bool strict_root_boundary_closure = options.enable_root_driver_sizing;
  const auto search_boundary_constraints
      = htree::ResolvePatternSearchBoundaryConstraints(base_boundary_constraints, strict_root_boundary_closure);

  const auto full_level_plans = htree::BuildLevelPlans(result.topology, char_flow.length_step_um, dbu_per_um);
  if (full_level_plans.empty()) {
    LOG_WARNING << "HTree: failed to derive H-tree level plans from topology.";
    build_stage.failed({{"reason", "empty_level_plans"}});
    return result;
  }

  const auto max_depth = static_cast<unsigned>(full_level_plans.size());
  const auto depth_candidates = htree::ResolveDepthCandidates(max_depth, options);
  if (depth_candidates.empty()) {
    LOG_WARNING << "HTree: no depth candidates were resolved from topology.";
    build_stage.failed({{"reason", "empty_depth_candidates"}});
    return result;
  }
  result.depth_explore_window = static_cast<unsigned>(depth_candidates.size());

  htree::BufferPatternLibrary segment_pattern_library;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_library.add(pattern);
  }

  auto required_segment_frontiers
      = htree::ResolveRequiredSegmentFrontiers(htree::CollectRequiredLengthIndices(full_level_plans), search_boundary_constraints);
  htree::SegmentFrontierCatalog segment_frontier_catalog;
  {
    auto segment_frontier_stage = SCHEMA_WRITER_INST.beginStage(
        "HTree", "Synthesize segment frontiers",
        {
            {"segment_chars", std::to_string(char_builder.get_segment_chars().size())},
            {"required_length_indices", std::to_string(required_segment_frontiers.required_length_indices.size())},
        },
        htree::DetailStageReportOptions());
    segment_frontier_catalog
        = htree::SynthesizeSegmentFrontiers(char_builder.get_segment_chars(), segment_pattern_library, required_segment_frontiers);
    if (segment_frontier_catalog.empty()) {
      LOG_WARNING << "HTree: segment frontier synthesis failed for the required aligned lengths.";
      segment_frontier_stage.failed({{"reason", "missing_required_segment_frontiers"}});
      build_stage.failed({{"reason", "missing_required_segment_frontiers"}});
      return result;
    }
    segment_frontier_stage.finished({
        {"length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
        {"frontier_entries", std::to_string(segment_frontier_catalog.countEntries(required_segment_frontiers.required_kinds))},
    });
  }

  const auto [root_driver_clock_period_ns, root_driver_clock_period_source] = htree::ResolveRootDriverClockPeriod(options);
  const htree::RootDriverCompensationOptions root_driver_compensation_options{
      .enabled = options.enable_root_driver_sizing,
      .input_slew_ns = htree::ResolveRootDriverCompensationInputSlewNs(options, char_builder.get_max_slew()),
      .clock_period_ns = root_driver_clock_period_ns,
      .cap_lattice = char_builder.get_cap_lattice(),
      .slew_lattice = char_builder.get_slew_lattice(),
      .default_cell_master = result.root_inst != nullptr ? result.root_inst->get_cell_master() : "",
      .strict_boundary_closure = strict_root_boundary_closure,
  };
  const htree::HTreeFanoutPruningOptions fanout_pruning_options{
      .max_fanout = CONFIG_INST.get_max_fanout(),
      .allow_boundary_relaxation = options.allow_boundary_relaxation,
  };
  result.analytical_mode_enabled = options.enable_analytical_solver;
  if (htree::analytical_solution::TryBuildAnalyticalHTreeResult(
          result, options, build_stage, max_depth, full_level_plans, depth_candidates, segment_frontier_catalog, segment_pattern_library,
          search_boundary_constraints, fanout_pruning_options, root_driver_compensation_options, char_builder,
          root_driver_clock_period_source)) {
    return result;
  }

  htree::DepthSearchResult exploration;
  {
    auto depth_search_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Search topology depth candidates",
                                        {
                                            {"depth_candidates", std::to_string(depth_candidates.size())},
                                            {"max_depth", std::to_string(max_depth)},
                                            {"segment_frontier_length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
                                        },
                                        htree::DetailStageReportOptions());
    exploration = htree::SearchTopologyDepthCandidates(result.topology, full_level_plans, depth_candidates, segment_frontier_catalog,
                                                       segment_pattern_library, search_boundary_constraints, char_builder.get_cap_lattice(),
                                                       result.char_slew_steps, options.target_depth.has_value(),
                                                       root_driver_compensation_options, fanout_pruning_options);
    depth_search_stage.finished({
        {"evaluated_depths", std::to_string(exploration.depth_summaries.size())},
        {"global_feasible_refs", std::to_string(exploration.global_feasible_pool.size())},
        {"global_candidate_refs", std::to_string(exploration.global_candidate_pool.size())},
        {"compensated_candidates", std::to_string(exploration.root_driver_compensation_stats.compensated_candidate_count)},
    });
  }
  result.depth_candidate_count = exploration.depth_summaries.size();

  htree::CandidateCharRefFilterResult covered_global_feasible_pool;
  htree::CandidateCharRefFilterResult covered_global_candidate_pool;
  {
    auto coverage_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Filter global sink-load coverage",
                                        {
                                            {"global_feasible_refs", std::to_string(exploration.global_feasible_pool.size())},
                                            {"global_candidate_refs", std::to_string(exploration.global_candidate_pool.size())},
                                        },
                                        htree::DetailStageReportOptions());
    covered_global_feasible_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.global_feasible_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
        exploration.sink_load_region_legality_context);
    covered_global_candidate_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.global_candidate_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
        exploration.sink_load_region_legality_context);
    coverage_stage.finished({
        {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.entries.size())},
        {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.entries.size())},
        {"first_feasible_failure",
         covered_global_feasible_pool.first_failure_reason.empty() ? "none" : covered_global_feasible_pool.first_failure_reason},
        {"first_candidate_failure",
         covered_global_candidate_pool.first_failure_reason.empty() ? "none" : covered_global_candidate_pool.first_failure_reason},
    });
  }

  std::vector<htree::CandidateCharRef> per_depth_feasible_pareto_pool;
  std::optional<htree::CandidateCharRef> selected_feasible_ref;
  std::optional<htree::CandidateCharRef> selected_relaxed_ref;
  {
    auto selection_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Select global topology",
                                        {
                                            {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.entries.size())},
                                            {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.entries.size())},
                                        },
                                        htree::DetailStageReportOptions());
    per_depth_feasible_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_feasible_pool.entries);
    selected_feasible_ref = htree::SelectBestGlobalEntry(per_depth_feasible_pareto_pool);
    std::size_t per_depth_candidate_pareto_count = 0U;
    if (!selected_feasible_ref.has_value() && options.allow_boundary_relaxation) {
      const auto per_depth_candidate_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_candidate_pool.entries);
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
    if (!selected_feasible_ref.has_value() && !options.allow_boundary_relaxation) {
      result.failure_reason = "no_strict_boundary_feasible_solution_any_depth";
    } else if (!covered_global_candidate_pool.first_failure_reason.empty()) {
      result.failure_reason = covered_global_candidate_pool.first_failure_reason;
    } else {
      result.failure_reason = exploration.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTree: failed to select a strict-feasible H-tree characterization entry across depth candidates.";
    build_stage.failed({{"reason", result.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  htree::EmitDepthCandidateSummary(exploration.depth_summaries);
  htree::SinkLoadRegionLegalityResult selected_sink_load_region_legality;
  {
    auto selected_legality_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Resolve selected sink-load legality",
                                        {
                                            {"selected_depth", std::to_string(selected_evaluation.depth)},
                                            {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                                        },
                                        htree::DetailStageReportOptions());
    selected_sink_load_region_legality = htree::ResolveSinkLoadRegionLegality(
        result.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library,
        exploration.sink_load_region_legality_context);
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
    result.failure_reason = "sink_load_region_legality_missing";
    LOG_WARNING << "HTree: selected global frontier entry is missing sink-load-region legality coverage.";
    build_stage.failed({{"reason", result.failure_reason}});
    return result;
  }
  selected_summary.htree_load_group_count = selected_sink_load_region_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_sink_load_region_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_sink_load_region_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_sink_load_region_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_sink_load_region_legality.cap_distribution.cap_median_pf;

  result.selected_depth = selected_evaluation.depth;
  result.best_char = *selected_ref->entry;
  htree::RootDriverCompensationPass selected_compensation_pass(root_driver_compensation_options);
  htree::RootDriverCompensationDetail selected_compensation_detail;
  {
    auto selected_compensation_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Resolve selected root-driver compensation",
                                        {
                                            {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                                            {"root_driver_sizing_enabled", options.enable_root_driver_sizing ? "true" : "false"},
                                        },
                                        htree::DetailStageReportOptions());
    selected_compensation_detail = selected_compensation_pass.evaluate(
        selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library, result.topology);
    selected_compensation_stage.finished({
        {"valid", selected_compensation_detail.valid ? "true" : "false"},
        {"cell_master", selected_compensation_detail.cell_master.empty() ? "none" : selected_compensation_detail.cell_master},
        {"load_cap_pf", std::to_string(selected_compensation_detail.load_cap_pf)},
    });
  }
  htree::ApplyRootDriverCompensationResult(result, exploration, selected_compensation_detail, *selected_ref->entry);
  result.root_driver_compensation.clock_period_source = root_driver_clock_period_source;
  result.levels = selected_evaluation.levels;
  result.selected_final_frontier_count = selected_summary.final_frontier_count;
  result.selected_candidate_solution_count = selected_summary.candidate_solution_count;
  result.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
  result.selected_feasible_solution_count = selected_summary.feasible_solution_count;
  result.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
  result.min_top_input_slew_ns = selected_evaluation.boundary_constraints.min_top_input_slew_ns;
  result.top_input_slew_covering_idx = selected_evaluation.boundary_constraints.top_input_slew_covering_idx;
  result.htree_load_group_count = selected_summary.htree_load_group_count;
  result.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
  result.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
  result.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
  result.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

  if (!selected_feasible_ref.has_value()) {
    result.used_boundary_relaxation = true;
    result.boundary_relaxation_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.boundary_relaxation_score
        = htree::CalcBoundaryRelaxationScore(*result.best_char, selected_evaluation.boundary_constraints, result.char_slew_steps);

    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "HTree",
                           "boundary relaxation is enabled; selected a relaxed solution from the global candidate pool.",
                           {
                               {"reason", result.boundary_relaxation_reason},
                               {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
                               {"relaxation_score", std::to_string(result.boundary_relaxation_score.value_or(0.0))},
                               {"selected_top_input_slew_idx", std::to_string(result.best_char->get_input_slew_idx())},
                               {"selected_leaf_load_cap_idx", std::to_string(result.best_char->get_leaf_load_cap_idx())},
                           });
  }

  result.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.best_char->get_pattern_id());
  htree::ApplySelectedPatternToLevelPlans(result, segment_pattern_library);
  const std::string selected_root_driver_cell_master = htree::ResolveSelectedRootDriverCellMaster(result.levels);
  if (options.enable_root_driver_sizing && !htree::ValidateRootDriverSizing(result, selected_root_driver_cell_master)) {
    result.failure_reason = "root_driver_sizing_precheck_failed";
    build_stage.failed({{"reason", result.failure_reason}});
    return result;
  }

  {
    auto embedding_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Build selected embedding",
                                                         {
                                                             {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
                                                             {"selected_levels", std::to_string(result.levels.size())},
                                                         },
                                                         htree::DetailStageReportOptions());
    htree::BuildEmbedding(result, segment_pattern_library);
    result.success = result.failure_reason.empty() && result.best_char.has_value() && result.best_pattern.has_value()
                     && result.root_output_pin != nullptr && result.root_net != nullptr;
    if (result.success && options.enable_root_driver_sizing) {
      LOG_FATAL_IF(!htree::ApplyRootDriverSizing(result, selected_root_driver_cell_master))
          << "HTree: prevalidated root-driver sizing failed during embedding construction.";
    } else if (result.success && result.root_inst != nullptr) {
      result.selected_root_driver_cell_master = result.root_inst->get_cell_master();
    }
    if (result.success) {
      embedding_stage.finished({
          {"inserted_insts", std::to_string(result.inserted_insts.size())},
          {"inserted_nets", std::to_string(result.inserted_nets.size())},
          {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers)},
      });
    } else {
      embedding_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
    }
  }

  {
    auto summary_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Emit synthesis summary", {}, schema::StageReportOptions{.emit_success_summary = false});
    htree::LogSynthesisSummary(result, selected_evaluation, selected_summary);
    summary_stage.finished();
  }
  if (result.success) {
    build_stage.finished();
  } else {
    build_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
  }
  return result;
}

}  // namespace icts
