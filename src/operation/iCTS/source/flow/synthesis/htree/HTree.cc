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
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "ClockRouteSegmentRc.hh"
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
#include "logger/Schema.hh"
#include "synthesis/htree/characterization/Characterization.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
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

namespace {

class HTreeBuilder
{
 public:
  HTreeBuilder(const HTree::Input& input, const HTree::Config& config) : _input(&input), _config(&config) {}

  auto build() -> htree::DiagnosticBuild;

 private:
  const HTree::Input* _input = nullptr;
  const HTree::Config* _config = nullptr;
};

auto ExtractProductionBuild(htree::DiagnosticBuild diagnostic_build) -> HTree::Build
{
  HTree::Build build;
  build.output = std::move(diagnostic_build.output);
  build.summary = std::move(diagnostic_build.summary);
  return build;
}

}  // namespace

auto HTreeBuilder::build() -> htree::DiagnosticBuild
{
  const auto& input = *_input;
  const auto& config = *_config;
  htree::DiagnosticBuild result;
  LOG_FATAL_IF(input.root_net == nullptr) << "HTree build requires an explicit root net.";
  auto& root_net = *input.root_net;
  result.diagnostics.log_context = input.log_context;
  result.diagnostics.object_name_prefix = input.object_name_prefix;
  result.output.root_net = &root_net;
  result.output.root_output_pin = root_net.get_driver();
  result.output.root_inst = result.output.root_output_pin == nullptr ? nullptr : result.output.root_output_pin->get_inst();
  if (result.output.root_output_pin == nullptr) {
    result.summary.failure_reason = "missing_root_driver_pin";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no driver pin.";
    return result;
  }

  const auto loads = root_net.get_loads();
  if (loads.empty()) {
    result.summary.failure_reason = "empty_root_net_loads";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no loads.";
    return result;
  }
  if (loads.size() == 1U) {
    const auto root = result.output.topology.create_node();
    result.output.topology.set_root(root);
    if (auto* root_node = result.output.topology.get_node(root); root_node != nullptr) {
      root_node->get_position() = input.fixed_topology_root_location.value_or(loads.front()->get_location());
    }
    result.summary.selected_depth = 0U;
    result.diagnostics.root_driver_sizing_enabled = config.enable_root_driver_sizing;
    result.diagnostics.target_depth = config.target_depth;
    result.summary.success = true;
    if (input.reporter != nullptr) {
      auto build_stage = input.reporter->beginStage("HTree", "build", {}, StageReportOptions{.emit_success_summary = false});
      build_stage.finished({
          {"reason", "trivial_single_load"},
          {"selected_depth", "0"},
          {"inserted_insts", "0"},
          {"inserted_nets", "0"},
      });
    }
    return result;
  }

  LOG_FATAL_IF(input.design == nullptr) << "HTree build requires an explicit design.";
  LOG_FATAL_IF(input.sta_adapter == nullptr) << "HTree build requires an explicit STA adapter.";
  LOG_FATAL_IF(input.reporter == nullptr) << "HTree build requires an explicit reporter.";
  LOG_FATAL_IF(input.characterization_input.fast_sta == nullptr) << "HTree build requires explicit FastSTA characterization context.";
  LOG_FATAL_IF(!input.characterization_input.dbu_per_um.has_value() || *input.characterization_input.dbu_per_um <= 0)
      << "HTree build requires explicit positive DBU-per-micron input.";

  auto& reporter = *input.reporter;
  auto build_stage = reporter.beginStage("HTree", "build", {}, StageReportOptions{.emit_success_summary = false});

  const int32_t dbu_per_um = *input.characterization_input.dbu_per_um;
  LOG_FATAL_IF(dbu_per_um <= 0) << "HTree: build failed because DBU-per-micron is unavailable.";

  BiPartitionConfig topology_config;
  topology_config.htree_topology_tolerance = std::max(0.0, config.topology_tolerance);
  topology_config.max_leaf_load_count = config.max_fanout;
  result.output.topology = TopologyGen::build(
      loads,
      TopologyGen::Input{
          .fixed_root_location = input.fixed_topology_root_location,
          .dbu_per_um = dbu_per_um,
          .load_count_kind
          = input.load_role == HTree::LoadRole::kLocalBuffer ? TopologyGen::LoadCountKind::kLocalBuffer : TopologyGen::LoadCountKind::kSink,
          .clock_name = input.log_context.clock_name,
          .clock_net_name = input.log_context.clock_net_name,
          .sink_domain = input.log_context.sink_domain,
          .stage = input.log_context.stage,
          .reporter = &reporter,
      },
      TopologyGen::Config{.partition_config = topology_config});
  const auto levels = result.output.topology.levels();
  if (levels.size() <= 1U) {
    LOG_WARNING << "HTree: topology has no H-tree levels after generation.";
    build_stage.skip({{"reason", "no_h_tree_levels"}});
    return result;
  }

  build_stage.markRunning("characterization");
  CharacterizationLibrary local_char_library;
  auto* char_library = input.characterization_library == nullptr ? &local_char_library : input.characterization_library;
  const auto char_input = input.characterization_input;
  const auto char_config = input.characterization_config;
  const auto char_flow
      = htree::RunCharacterizationFlow(result.output.topology, dbu_per_um, char_input, char_config, result, *char_library, input, config);
  if (!char_flow.success) {
    build_stage.failed({{"reason", char_flow.failure_reason}});
    return result;
  }
  const auto& char_builder = char_library->getCharBuilder();

  const auto base_boundary_constraints = htree::ResolveBoundaryConstraints(config, char_builder);
  result.diagnostics.force_branch_buffer = base_boundary_constraints.force_branch_buffer;
  result.diagnostics.root_driver_sizing_enabled = config.enable_root_driver_sizing;
  result.diagnostics.target_depth = config.target_depth;
  const bool strict_root_boundary_closure = config.enable_root_driver_sizing;
  const auto search_boundary_constraints
      = htree::ResolvePatternSearchBoundaryConstraints(base_boundary_constraints, strict_root_boundary_closure);

  const auto full_level_plans = htree::BuildLevelPlans(result.output.topology, char_flow.length_step_um, dbu_per_um);
  if (full_level_plans.empty()) {
    LOG_WARNING << "HTree: failed to derive H-tree level plans from topology.";
    build_stage.failed({{"reason", "empty_level_plans"}});
    return result;
  }

  const auto max_depth = static_cast<unsigned>(full_level_plans.size());
  const auto depth_candidates = htree::ResolveDepthCandidates(max_depth, config);
  if (depth_candidates.empty()) {
    LOG_WARNING << "HTree: no depth candidates were resolved from topology.";
    build_stage.failed({{"reason", "empty_depth_candidates"}});
    return result;
  }
  result.diagnostics.depth_explore_window = static_cast<unsigned>(depth_candidates.size());

  htree::BufferPatternLibrary segment_pattern_library(*input.sta_adapter);
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_library.add(pattern);
  }

  htree::SegmentFrontierCatalog segment_frontier_catalog;
  if (!config.enable_analytical_solver) {
    auto required_segment_frontiers
        = htree::ResolveRequiredSegmentFrontiers(htree::CollectRequiredLengthIndices(full_level_plans), search_boundary_constraints);
    auto segment_frontier_stage
        = reporter.beginStage("HTree", "Synthesize segment frontiers",
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
  } else {
    auto segment_frontier_stage = reporter.beginStage("HTree", "Synthesize segment frontiers",
                                                      {
                                                          {"segment_chars", std::to_string(char_builder.get_segment_chars().size())},
                                                          {"required_length_indices", "0"},
                                                      },
                                                      htree::DetailStageReportOptions());
    segment_frontier_stage.skip({{"reason", "mathematical_analytical_solver_uses_unit_affine_models"}});
  }

  const auto [root_driver_clock_period_ns, root_driver_clock_period_source] = htree::ResolveRootDriverClockPeriod(input);
  const htree::RootDriverCompensationInput root_driver_compensation_input{
      .enabled = config.enable_root_driver_sizing,
      .sta_adapter = input.sta_adapter,
      .input_slew_ns = htree::ResolveRootDriverCompensationInputSlewNs(config, char_builder.get_max_slew()),
      .clock_period_ns = root_driver_clock_period_ns,
      .cap_lattice = char_builder.get_cap_lattice(),
      .slew_lattice = char_builder.get_slew_lattice(),
      .default_cell_master = result.output.root_inst != nullptr ? result.output.root_inst->get_cell_master() : "",
      .routing_layer = config.routing_layer,
      .wire_width_um = config.wire_width_um,
      .dbu_per_um = dbu_per_um,
      .reporter = &reporter,
      .strict_boundary_closure = strict_root_boundary_closure,
  };
  const htree::HTreeFanoutPruningConfig fanout_pruning_config{
      .max_fanout = config.max_fanout,
      .allow_boundary_relaxation = config.allow_boundary_relaxation,
  };
  const htree::SinkLoadRegionLegalityInput sink_load_region_input{
      .sta_adapter = input.sta_adapter,
      .max_fanout = config.max_fanout,
      .has_max_cap = config.has_max_cap,
      .max_cap_pf = config.has_max_cap ? config.max_cap_pf : std::numeric_limits<double>::infinity(),
      .clock_route_segment_rc = char_builder.get_clock_route_segment_rc(),
  };
  result.diagnostics.analytical_mode_enabled = config.enable_analytical_solver;
  if (htree::analytical_solution::TryBuildAnalyticalHTree(result, input, config, build_stage, max_depth, full_level_plans, depth_candidates,
                                                          segment_frontier_catalog, segment_pattern_library, search_boundary_constraints,
                                                          fanout_pruning_config, root_driver_compensation_input, sink_load_region_input,
                                                          char_builder, root_driver_clock_period_source)) {
    return result;
  }

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
    if (!selected_feasible_ref.has_value() && !config.allow_boundary_relaxation) {
      result.summary.failure_reason = "no_strict_boundary_feasible_solution_any_depth";
    } else if (!covered_global_candidate_pool.summary.first_failure_reason.empty()) {
      result.summary.failure_reason = covered_global_candidate_pool.summary.first_failure_reason;
    } else {
      result.summary.failure_reason = exploration.output.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTree: failed to select a strict-feasible H-tree characterization entry across depth candidates.";
    build_stage.failed({{"reason", result.summary.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
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
    result.summary.failure_reason = "sink_load_region_legality_missing";
    LOG_WARNING << "HTree: selected global frontier entry is missing sink-load-region legality coverage.";
    build_stage.failed({{"reason", result.summary.failure_reason}});
    return result;
  }
  selected_summary.htree_load_group_count = selected_sink_load_region_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_sink_load_region_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_sink_load_region_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_sink_load_region_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_sink_load_region_legality.cap_distribution.cap_median_pf;

  result.summary.selected_depth = selected_evaluation.depth;
  result.output.best_char = *selected_ref->entry;
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
  htree::ApplyRootDriverCompensationSummary(result, exploration, selected_compensation_detail, *selected_ref->entry);
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

  if (!selected_feasible_ref.has_value()) {
    result.summary.used_boundary_relaxation = true;
    result.diagnostics.boundary_relaxation_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.diagnostics.boundary_relaxation_score = htree::CalcBoundaryRelaxationScore(
        *result.output.best_char, selected_evaluation.boundary_constraints, result.diagnostics.char_slew_steps);

    EmitDiagnostic(reporter, DiagnosticLevel::kWarning, "HTree",
                   "boundary relaxation is enabled; selected a relaxed solution from the global candidate pool.",
                   {
                       {"reason", result.diagnostics.boundary_relaxation_reason},
                       {"selected_depth", std::to_string(result.summary.selected_depth.value_or(0U))},
                       {"relaxation_score", std::to_string(result.diagnostics.boundary_relaxation_score.value_or(0.0))},
                       {"selected_top_input_slew_idx", std::to_string(result.output.best_char->get_input_slew_idx())},
                       {"selected_leaf_load_cap_idx", std::to_string(result.output.best_char->get_leaf_load_cap_idx())},
                   });
  }

  result.output.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.output.best_char->get_pattern_id());
  htree::ApplySelectedPatternToLevelPlans(*input.sta_adapter, result, segment_pattern_library);
  const std::string selected_root_driver_cell_master = htree::ResolveSelectedRootDriverCellMaster(result.output.levels);
  if (config.enable_root_driver_sizing
      && !htree::ValidateRootDriverSizing(*input.design, *input.sta_adapter, result, selected_root_driver_cell_master)) {
    result.summary.failure_reason = "root_driver_sizing_precheck_failed";
    build_stage.failed({{"reason", result.summary.failure_reason}});
    return result;
  }

  {
    auto embedding_stage = reporter.beginStage("HTree", "Build selected embedding",
                                               {
                                                   {"selected_depth", std::to_string(result.summary.selected_depth.value_or(0U))},
                                                   {"selected_levels", std::to_string(result.output.levels.size())},
                                               },
                                               htree::DetailStageReportOptions());
    htree::BuildEmbedding(*input.design, *input.sta_adapter, result, segment_pattern_library);
    result.summary.success = result.summary.failure_reason.empty() && result.output.best_char.has_value()
                             && result.output.best_pattern.has_value() && result.output.root_output_pin != nullptr
                             && result.output.root_net != nullptr;
    if (result.summary.success && config.enable_root_driver_sizing) {
      LOG_FATAL_IF(!htree::ApplyRootDriverSizing(*input.design, *input.sta_adapter, result, selected_root_driver_cell_master))
          << "HTree: prevalidated root-driver sizing failed during embedding construction.";
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
    build_stage.finished();
  } else {
    build_stage.failed({{"reason", result.summary.failure_reason.empty() ? "incomplete_embedding_build" : result.summary.failure_reason}});
  }
  return result;
}

auto HTree::build(const Input& input, const Config& config) -> Build
{
  return ExtractProductionBuild(htree::BuildWithDiagnostics(input, config));
}

auto htree::BuildWithDiagnostics(const HTree::Input& input, const HTree::Config& config) -> htree::DiagnosticBuild
{
  HTreeBuilder builder(input, config);
  return builder.build();
}

}  // namespace icts
