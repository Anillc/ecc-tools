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
 * @file ClockSourceRootSynthesizer.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements ClockSynthesis source-to-root segment or HTree dispatch.
 */

#include "synthesis/ClockSourceRootSynthesizer.hh"

#include <vector>

#include "Log.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/SegmentBuilder.hh"
#include "synthesis/ClockSynthesisHtreeOptions.hh"
#include "synthesis/ClockSynthesisNetEditor.hh"
#include "synthesis/ClockSynthesisResultAccounting.hh"

namespace icts::clock_synthesis {

auto BuildSourceToRootTree(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                           const ClockSynthesis::SourceToRootBuildOptions& options) -> ClockSynthesis::SourceToRootBuildResult
{
  ClockSynthesis::SourceToRootBuildResult result;
  if (clock_source == nullptr) {
    result.failure_reason = "clock_source_is_null";
    LOG_ERROR << "ClockSynthesis: top-level source-to-root synthesis failed because clock source is null.";
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
    LOG_ERROR << "ClockSynthesis: top-level source-to-root synthesis failed because no root inputs are available.";
    return result;
  }

  SourceNetSideEffectGuard source_net_side_effects(source_net, clock_source, valid_root_inputs);
  ReconnectExistingNet(source_net, clock_source, valid_root_inputs);
  if (valid_root_inputs.size() == 1U) {
    result.stage = ClockSynthesis::SourceToRootStage::kSegment;
    auto segment_options = BuildTopSegmentOptions(clock_source, valid_root_inputs.front(), options);
    auto segment_result = SegmentBuilder::build(source_net, clock_source, valid_root_inputs.front(), segment_options);
    if (!segment_result.success) {
      result.failure_reason = segment_result.failure_reason.empty() ? "top_segment_failed" : segment_result.failure_reason;
      result.used_boundary_fallback = segment_result.used_boundary_fallback;
      source_net_side_effects.restore();
      return result;
    }
    RecordTopSegmentResult(result, segment_result);
    return result;
  }

  result.stage = ClockSynthesis::SourceToRootStage::kHTree;
  auto htree_options = BuildTopHtreeOptions(clock_source, options);
  result.htree_result = HTreeBuilder::build(source_net, htree_options);
  if (!result.htree_result.success) {
    result.failure_reason = result.htree_result.failure_reason.empty() ? "top_htree_failed" : result.htree_result.failure_reason;
    source_net_side_effects.restore();
    return result;
  }

  RecordTopHtreeResult(result);
  return result;
}

}  // namespace icts::clock_synthesis
