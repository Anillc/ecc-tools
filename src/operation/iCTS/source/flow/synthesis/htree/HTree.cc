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
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "ClockRouteSegmentRc.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
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
#include "synthesis/htree/plan/Plan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "synthesis/htree/solution/Solution.hh"
#include "synthesis/htree/solution/report/StageReport.hh"
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
  if (config.enable_analytical_solver) {
    auto analytical_selection = htree::analytical_solution::SelectAnalyticalHTreeSolution(
        result, input, max_depth, full_level_plans, depth_candidates, segment_pattern_library, search_boundary_constraints,
        fanout_pruning_config, root_driver_compensation_input, sink_load_region_input, char_builder, root_driver_clock_period_source);
    if (analytical_selection.selected) {
      htree::FinalizeSelectedHTreeSolution(result, input, config, build_stage, analytical_selection.selected_solution,
                                           segment_pattern_library);
    } else {
      result.summary.failure_reason
          = analytical_selection.failure_reason.empty() ? "analytical_candidate_unavailable" : analytical_selection.failure_reason;
      build_stage.failed({{"reason", result.summary.failure_reason}, {"selection_engine", "analytical"}});
    }
    return result;
  }

  auto discrete_selection = htree::discrete_solution::SelectDiscreteHTreeSolution(
      result, config, reporter, max_depth, full_level_plans, depth_candidates, segment_frontier_catalog, segment_pattern_library,
      search_boundary_constraints, fanout_pruning_config, root_driver_compensation_input, sink_load_region_input, char_builder,
      root_driver_clock_period_source);
  if (discrete_selection.selected) {
    htree::FinalizeSelectedHTreeSolution(result, input, config, build_stage, discrete_selection.selected_solution, segment_pattern_library);
  } else {
    result.summary.failure_reason = discrete_selection.failure_reason.empty() ? "missing_best_char" : discrete_selection.failure_reason;
    build_stage.failed({{"reason", result.summary.failure_reason},
                        {"depth_candidates", std::to_string(depth_candidates.size())},
                        {"selection_engine", "discrete"}});
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
