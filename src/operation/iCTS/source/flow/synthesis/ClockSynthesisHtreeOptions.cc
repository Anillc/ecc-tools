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
 * @file ClockSynthesisHtreeOptions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements HTree and source-to-root option construction for ClockSynthesis.
 */

#include "synthesis/ClockSynthesisHtreeOptions.hh"

#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "synthesis/ClockSynthesisNetEditor.hh"

namespace icts::clock_synthesis {
namespace {

auto resolveSourceDriveCap(Pin* clock_source) -> double
{
  return STA_ADAPTER_INST.queryClockSourceDriveCapLimit(clock_source);
}

auto applyMinTopInputSlew(HTreeBuilder::BuildOptions& htree_options) -> void
{
  const double max_buf_tran = CONFIG_INST.get_max_buf_tran();
  if (max_buf_tran > 0.0) {
    htree_options.min_top_input_slew_ns = max_buf_tran * 0.5;
  }
}

auto applyMinInputSlew(SourceToRootSegmentBuilder::BuildOptions& segment_options) -> void
{
  const double max_buf_tran = CONFIG_INST.get_max_buf_tran();
  if (max_buf_tran > 0.0) {
    segment_options.min_input_slew_ns = max_buf_tran * 0.5;
  }
}

}  // namespace

auto BuildSinkHtreeOptions(bool enable_sink_clustering, const ClockSynthesis::BuildOptions& options) -> HTreeBuilder::BuildOptions
{
  HTreeBuilder::BuildOptions htree_options;
  applyMinTopInputSlew(htree_options);
  htree_options.topology_loads_are_local_buffers = enable_sink_clustering;
  htree_options.characterization_library = options.characterization_library;
  htree_options.additional_characterization_lengths_um = options.additional_characterization_lengths_um;
  htree_options.log_context = options.log_context;
  htree_options.object_name_prefix = options.object_name_prefix;
  return htree_options;
}

auto BuildTopSegmentOptions(Pin* clock_source, Pin* root_input, const ClockSynthesis::SourceToRootBuildOptions& options)
    -> SourceToRootSegmentBuilder::BuildOptions
{
  HTreeBuilder::LogContext log_context = options.log_context;
  log_context.stage = "top_segment";
  SourceToRootSegmentBuilder::BuildOptions segment_options{
      .characterization_library = options.characterization_library,
      .required_load_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(root_input),
      .source_drive_cap_pf = resolveSourceDriveCap(clock_source),
      .object_name_prefix = options.object_name_prefix,
      .log_context = log_context,
  };
  applyMinInputSlew(segment_options);
  return segment_options;
}

auto BuildTopHtreeOptions(Pin* clock_source, const ClockSynthesis::SourceToRootBuildOptions& options) -> HTreeBuilder::BuildOptions
{
  HTreeBuilder::LogContext log_context = options.log_context;
  log_context.stage = "top_htree";
  HTreeBuilder::BuildOptions htree_options;
  applyMinTopInputSlew(htree_options);
  htree_options.fixed_topology_root_location = FindRenderableLocation(clock_source);
  htree_options.characterization_library = options.characterization_library;
  htree_options.enable_root_driver_sizing = false;
  htree_options.object_name_prefix = options.object_name_prefix;
  htree_options.log_context = log_context;
  return htree_options;
}

}  // namespace icts::clock_synthesis
