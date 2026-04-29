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
 * @file ClockSynthesis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Orchestrates optional sink clustering and H-tree synthesis for one clock distribution.
 */

#include "synthesis/ClockSynthesis.hh"

#include "synthesis/ClockSinkTreeSynthesizer.hh"
#include "synthesis/ClockSourceRootSynthesizer.hh"

namespace icts {

auto ClockSynthesis::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto ClockSynthesis::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  return clock_synthesis::BuildSinkTree(root_net, options);
}

auto ClockSynthesis::buildSourceToRoot(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                                       const SourceToRootBuildOptions& options) -> SourceToRootBuildResult
{
  return clock_synthesis::BuildSourceToRootTree(source_net, clock_source, root_inputs, options);
}

auto ToString(ClockSynthesis::SourceToRootStage stage) -> const char*
{
  switch (stage) {
    case ClockSynthesis::SourceToRootStage::kSegment:
      return "top_segment";
    case ClockSynthesis::SourceToRootStage::kHTree:
      return "top_htree";
    case ClockSynthesis::SourceToRootStage::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace icts
