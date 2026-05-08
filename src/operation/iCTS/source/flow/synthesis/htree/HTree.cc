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
#include "CharBuilder.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
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
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "synthesis/htree/solution/SolutionReport.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"
#include "topology/TopologyGen.hh"

namespace icts {

namespace {

constexpr double kRootDriverCompensationClockPeriodNs = 10.0;

auto ResolveSelectedRootDriverCellMaster(const std::vector<HTree::LevelPlan>& levels) -> std::string
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

auto ResolveRootDriverCompensationInputSlewNs(double max_slew_ns) -> double
{
  return max_slew_ns > 0.0 ? max_slew_ns * 0.5 : 0.0;
}

auto ResolveRootDriverClockPeriod(const HTree::BuildOptions& options) -> std::pair<double, std::string>
{
  if (options.clock_period_ns > 0.0) {
    return {options.clock_period_ns, options.clock_period_source.empty() ? "caller" : options.clock_period_source};
  }
  return {kRootDriverCompensationClockPeriodNs, "default_10ns"};
}

auto ApplyRootDriverCompensationResult(HTree::BuildResult& result, const htree::DepthSearchResult& exploration,
                                       const htree::RootDriverCompensationDetail& compensation_detail,
                                       const HTreeTopologyChar& selected_entry) -> void
{
  auto& report = result.root_driver_compensation;
  report.enabled = exploration.root_driver_compensation_stats.enabled;
  report.valid = compensation_detail.valid;
  report.method = compensation_detail.method.empty() ? exploration.root_driver_compensation_stats.method : compensation_detail.method;
  report.cell_master = compensation_detail.cell_master;
  report.load_source
      = compensation_detail.load_source.empty() ? exploration.root_driver_compensation_stats.load_source : compensation_detail.load_source;
  report.route_estimator = compensation_detail.route_estimator;
  report.input_slew_ns = compensation_detail.input_slew_ns > 0.0 ? compensation_detail.input_slew_ns
                                                                 : exploration.root_driver_compensation_stats.input_slew_ns;
  report.load_bucket_idx = compensation_detail.load_bucket_idx;
  report.load_cap_pf = compensation_detail.load_cap_pf;
  report.terminal_pin_cap_pf = compensation_detail.terminal_pin_cap_pf;
  report.wire_cap_pf = compensation_detail.wire_cap_pf;
  report.routed_wirelength_um = compensation_detail.routed_wirelength_um;
  report.terminal_count = compensation_detail.terminal_count;
  report.clock_period_ns = compensation_detail.clock_period_ns > 0.0 ? compensation_detail.clock_period_ns
                                                                     : exploration.root_driver_compensation_stats.clock_period_ns;
  report.cell_delay_ns = compensation_detail.cell_delay_ns;
  report.internal_power_w = compensation_detail.internal_power_w;
  report.leakage_power_w = compensation_detail.leakage_power_w;
  report.cell_power_w = compensation_detail.cell_power_w;
  report.raw_delay_ns = selected_entry.get_raw_delay();
  report.raw_power_w = selected_entry.get_raw_power();
  report.compensated_delay_ns = selected_entry.get_delay();
  report.compensated_power_w = selected_entry.get_power();
}

}  // namespace

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
  auto build_stage = SCHEMA_WRITER_INST.beginStage("HTree", "build");
  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});

  BiPartitionConfig topology_config;
  topology_config.htree_topology_tolerance
      = std::max(0.0, options.htree_topology_tolerance.value_or(CONFIG_INST.get_htree_topology_tolerance()));
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

  const auto required_length_indices = htree::CollectRequiredLengthIndices(full_level_plans);
  auto entry_sets_by_length
      = htree::SynthesizeSegmentEntrySets(char_builder.get_segment_chars(), segment_pattern_library, required_length_indices);
  if (entry_sets_by_length.empty()) {
    LOG_WARNING << "HTree: segment frontier synthesis failed for the required aligned lengths.";
    build_stage.failed({{"reason", "missing_required_segment_frontiers"}});
    return result;
  }

  const auto [root_driver_clock_period_ns, root_driver_clock_period_source] = ResolveRootDriverClockPeriod(options);
  const htree::RootDriverCompensationOptions root_driver_compensation_options{
      .enabled = options.enable_root_driver_sizing,
      .input_slew_ns = ResolveRootDriverCompensationInputSlewNs(char_builder.get_max_slew()),
      .clock_period_ns = root_driver_clock_period_ns,
      .cap_lattice = char_builder.get_cap_lattice(),
      .fallback_cell_master = result.root_inst != nullptr ? result.root_inst->get_cell_master() : "",
  };
  auto exploration = htree::SearchTopologyDepthCandidates(
      result.topology, full_level_plans, depth_candidates, entry_sets_by_length, segment_pattern_library, base_boundary_constraints,
      char_builder.get_cap_lattice(), result.char_slew_steps, options.target_depth.has_value(), root_driver_compensation_options);
  result.depth_candidate_count = exploration.depth_summaries.size();

  const auto covered_global_feasible_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
      exploration.global_feasible_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
      exploration.sink_load_region_legality_context);
  const auto covered_global_candidate_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
      exploration.global_candidate_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
      exploration.sink_load_region_legality_context);

  const auto selected_feasible_ref = htree::SelectBestGlobalEntry(covered_global_feasible_pool.entries);
  const auto selected_fallback_ref = selected_feasible_ref.has_value()
                                         ? std::optional<htree::CandidateCharRef>{}
                                         : htree::SelectBestGlobalEntry(covered_global_candidate_pool.entries);
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_fallback_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    if (!covered_global_candidate_pool.first_failure_reason.empty()) {
      result.failure_reason = covered_global_candidate_pool.first_failure_reason;
    } else {
      result.failure_reason = exploration.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTree: failed to select a best H-tree characterization entry across depth candidates.";
    build_stage.failed({{"reason", result.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  const auto selected_sink_load_region_legality = htree::ResolveSinkLoadRegionLegality(
      result.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library,
      exploration.sink_load_region_legality_context);
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
  const auto selected_compensation_detail = selected_compensation_pass.evaluate(
      selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library, result.topology);
  ApplyRootDriverCompensationResult(result, exploration, selected_compensation_detail, *selected_ref->entry);
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
    result.used_boundary_fallback = true;
    result.boundary_fallback_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.boundary_fallback_score
        = htree::CalcBoundaryFallbackScore(*result.best_char, selected_evaluation.boundary_constraints, result.char_slew_steps);

    schema::EmitDiagnostic(
        schema::DiagnosticLevel::kFallback, "HTree",
        "no depth candidate satisfied caller boundary constraints; selected fallback solution from the global candidate pool.",
        {
            {"reason", result.boundary_fallback_reason},
            {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
            {"fallback_score", std::to_string(result.boundary_fallback_score.value_or(0.0))},
            {"selected_top_input_slew_idx", std::to_string(result.best_char->get_input_slew_idx())},
            {"selected_leaf_load_cap_idx", std::to_string(result.best_char->get_leaf_load_cap_idx())},
        });
  }

  result.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.best_char->get_pattern_id());
  const auto& best_level_segment_pattern_ids = result.best_pattern->get_level_segment_pattern_ids();
  LOG_FATAL_IF(best_level_segment_pattern_ids.size() != result.levels.size())
      << "HTree: best H-tree pattern level count does not match selected depth.";
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto segment_pattern_id = best_level_segment_pattern_ids.at(level_index);
    result.levels.at(level_index).segment_pattern_id = segment_pattern_id;
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTree: selected segment pattern metadata is missing.";
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
  if (options.enable_root_driver_sizing && !htree::ValidateRootDriverSizing(result, selected_root_driver_cell_master)) {
    result.failure_reason = "root_driver_sizing_precheck_failed";
    build_stage.failed({{"reason", result.failure_reason}});
    return result;
  }

  htree::BuildEmbedding(result, segment_pattern_library);
  result.success = result.failure_reason.empty() && result.best_char.has_value() && result.best_pattern.has_value()
                   && result.root_output_pin != nullptr && result.root_net != nullptr;
  if (result.success && options.enable_root_driver_sizing) {
    LOG_FATAL_IF(!htree::ApplyRootDriverSizing(result, selected_root_driver_cell_master))
        << "HTree: prevalidated root-driver sizing failed during embedding construction.";
  } else if (result.success && result.root_inst != nullptr) {
    result.selected_root_driver_cell_master = result.root_inst->get_cell_master();
  }

  htree::LogSynthesisSummary(result, selected_evaluation, selected_summary);
  if (result.success) {
    build_stage.finished();
  } else {
    build_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
  }
  return result;
}

}  // namespace icts
