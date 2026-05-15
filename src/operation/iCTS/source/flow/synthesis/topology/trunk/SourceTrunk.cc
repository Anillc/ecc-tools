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
 * @file SourceTrunk.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements Topology source-to-root segment or HTree dispatch.
 */

#include "synthesis/topology/trunk/SourceTrunk.hh"

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "Point.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "synthesis/topology/trunk/SourceTrunkSegment.hh"
#include "synthesis/trace/topology_result/TopologyResult.hh"

namespace icts {
class Net;
class Pin;
}  // namespace icts

namespace icts::topology {

namespace {

auto ResolveSourceDriveCap(Pin* clock_source) -> double
{
  return STA_ADAPTER_INST.queryClockSourceDriveCapLimit(clock_source);
}

auto ApplyMinTopInputSlew(HTree::BuildOptions& htree_options) -> void
{
  htree_options.min_top_input_slew_ns = CONFIG_INST.get_root_input_slew();
}

auto ApplyMinInputSlew(SourceTrunkSegment::BuildOptions& segment_options) -> void
{
  segment_options.min_input_slew_ns = CONFIG_INST.get_root_input_slew();
}

auto BuildTopSegmentOptions(Pin* clock_source, Pin* root_input, const Topology::SourceTrunkBuildOptions& options)
    -> SourceTrunkSegment::BuildOptions
{
  HTree::LogContext log_context = options.log_context;
  log_context.stage = "top_segment";
  SourceTrunkSegment::BuildOptions segment_options{
      .characterization_library = options.characterization_library,
      .required_load_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(root_input),
      .source_drive_cap_pf = ResolveSourceDriveCap(clock_source),
      .object_name_prefix = options.object_name_prefix,
      .log_context = log_context,
  };
  ApplyMinInputSlew(segment_options);
  return segment_options;
}

auto BuildTopHtreeOptions(Pin* clock_source, const Topology::SourceTrunkBuildOptions& options) -> HTree::BuildOptions
{
  HTree::LogContext log_context = options.log_context;
  log_context.stage = "top_htree";
  HTree::BuildOptions htree_options;
  ApplyMinTopInputSlew(htree_options);
  htree_options.fixed_topology_root_location = FindRenderableLocation(clock_source);
  htree_options.characterization_library = options.characterization_library;
  htree_options.enable_root_driver_sizing = false;
  htree_options.clock_period_ns = options.clock_period_ns;
  htree_options.clock_period_source = options.clock_period_source;
  htree_options.object_name_prefix = options.object_name_prefix;
  htree_options.log_context = log_context;
  htree_options.enable_analytical_solver = CONFIG_INST.is_enable_analytical_htree();
  return htree_options;
}

auto DetailStageReportOptions() -> schema::StageReportOptions
{
  return schema::StageReportOptions{.context_sink = schema::ReportSink::kDetail, .summary_sink = schema::ReportSink::kDetail};
}

}  // namespace

auto BuildSourceTrunkTree(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                          const Topology::SourceTrunkBuildOptions& options) -> Topology::SourceTrunkBuildResult
{
  Topology::SourceTrunkBuildResult result;
  if (clock_source == nullptr) {
    result.failure_reason = "clock_source_is_null";
    LOG_ERROR << "Topology: top-level source-to-root synthesis failed because clock source is null.";
    return result;
  }

  std::vector<Pin*> valid_root_inputs;
  valid_root_inputs.reserve(root_inputs.size());
  for (auto* root_input : root_inputs) {
    if (root_input != nullptr) {
      valid_root_inputs.push_back(root_input);
    }
  }
  if (valid_root_inputs.empty()) {
    result.failure_reason = "empty_root_inputs";
    LOG_ERROR << "Topology: top-level source-to-root synthesis failed because no root inputs are available.";
    return result;
  }

  SourceNetSideEffectGuard source_net_side_effects(source_net, clock_source, valid_root_inputs);
  ReconnectExistingNet(source_net, clock_source, valid_root_inputs);
  auto dispatch_stage = SCHEMA_WRITER_INST.beginStage("SourceTrunk", "Dispatch source trunk synthesis",
                                                      {
                                                          {"root_inputs", std::to_string(valid_root_inputs.size())},
                                                          {"dispatch", valid_root_inputs.size() == 1U ? "top_segment" : "top_htree"},
                                                      },
                                                      DetailStageReportOptions());
  if (valid_root_inputs.size() == 1U) {
    result.stage = Topology::SourceTrunkStage::kSegment;
    auto segment_options = BuildTopSegmentOptions(clock_source, valid_root_inputs.front(), options);
    auto segment_result = SourceTrunkSegment::build(source_net, clock_source, valid_root_inputs.front(), segment_options);
    if (!segment_result.success) {
      result.failure_reason = segment_result.failure_reason.empty() ? "top_segment_failed" : segment_result.failure_reason;
      result.used_boundary_fallback = segment_result.used_boundary_fallback;
      source_net_side_effects.restore();
      dispatch_stage.failed({{"reason", result.failure_reason}});
      return result;
    }
    RecordTopSegmentResult(result, segment_result);
    dispatch_stage.finished({
        {"stage", ToString(result.stage)},
        {"inserted_insts", std::to_string(result.inserted_insts.size())},
        {"inserted_nets", std::to_string(result.inserted_nets.size())},
        {"used_boundary_fallback", result.used_boundary_fallback ? "true" : "false"},
    });
    return result;
  }

  result.stage = Topology::SourceTrunkStage::kHTree;
  auto htree_options = BuildTopHtreeOptions(clock_source, options);
  result.htree_result = HTree::build(source_net, htree_options);
  if (!result.htree_result.success) {
    result.failure_reason = result.htree_result.failure_reason.empty() ? "top_htree_failed" : result.htree_result.failure_reason;
    source_net_side_effects.restore();
    dispatch_stage.failed({{"reason", result.failure_reason}});
    return result;
  }

  RecordTopHtreeResult(result);
  dispatch_stage.finished({
      {"stage", ToString(result.stage)},
      {"selected_depth", std::to_string(result.htree_result.selected_depth.value_or(0U))},
      {"inserted_insts", std::to_string(result.inserted_insts.size())},
      {"inserted_nets", std::to_string(result.inserted_nets.size())},
      {"used_boundary_fallback", result.used_boundary_fallback ? "true" : "false"},
  });
  return result;
}

}  // namespace icts::topology
