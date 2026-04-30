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
 * @file ClockSynthesisHtreeOptions.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Builds HTree and source-to-root option objects for ClockSynthesis.
 */

#pragma once

#include "htree/SourceToRootSegmentBuilder.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts::clock_synthesis {

auto BuildSinkHtreeOptions(bool enable_sink_clustering, const ClockSynthesis::BuildOptions& options) -> HTreeBuilder::BuildOptions;
auto BuildTopSegmentOptions(Pin* clock_source, Pin* root_input, const ClockSynthesis::SourceToRootBuildOptions& options)
    -> SourceToRootSegmentBuilder::BuildOptions;
auto BuildTopHtreeOptions(Pin* clock_source, const ClockSynthesis::SourceToRootBuildOptions& options) -> HTreeBuilder::BuildOptions;

}  // namespace icts::clock_synthesis
