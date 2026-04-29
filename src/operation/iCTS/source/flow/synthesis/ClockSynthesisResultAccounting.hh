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
 * @file ClockSynthesisResultAccounting.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Records ClockSynthesis result counters and transfers temporary object ownership.
 */

#pragma once

#include "htree/SegmentBuilder.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts::clock_synthesis {

auto RecordSinkHtreeResult(ClockSynthesis::BuildResult& result) -> void;
auto RecordTopSegmentResult(ClockSynthesis::SourceToRootBuildResult& result, SegmentBuilder::BuildResult& segment_result) -> void;
auto RecordTopHtreeResult(ClockSynthesis::SourceToRootBuildResult& result) -> void;

}  // namespace icts::clock_synthesis
