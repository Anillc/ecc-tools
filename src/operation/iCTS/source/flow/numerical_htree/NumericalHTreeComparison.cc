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
 * @file NumericalHTreeComparison.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Compares numerical H-tree QoR against native H-tree reference results.
 */

#include <algorithm>
#include <limits>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto RelativeDelta(double lhs, double rhs) -> double
{
  if (rhs <= 0.0 || !numerical_htree::IsFinite(lhs) || !numerical_htree::IsFinite(rhs)) {
    return std::numeric_limits<double>::infinity();
  }
  return std::max(0.0, lhs - rhs) / rhs;
}

}  // namespace

auto NumericalHTreeBuilder::compareWithNative(const NumericalHTreeResult& numerical_result,
                                              const NumericalHTreeNativeReference& native_reference, double delay_relative_tolerance,
                                              double power_relative_tolerance) -> NumericalHTreeComparison
{
  NumericalHTreeComparison comparison;
  comparison.numerical_success = numerical_result.success;
  comparison.native_available = native_reference.available;
  comparison.numerical_delay_ns = numerical_result.selected_delay_ns;
  comparison.numerical_power_w = numerical_result.selected_power_w;
  comparison.numerical_runtime_ms = numerical_result.runtime_ms;
  comparison.native_delay_ns = native_reference.delay_ns;
  comparison.native_power_w = native_reference.power_w;
  comparison.native_runtime_ms = native_reference.runtime_ms;
  comparison.numerical_segment_pattern_ids = numerical_result.selected_segment_pattern_ids;
  comparison.native_segment_pattern_ids = native_reference.segment_pattern_ids;

  if (!numerical_result.success || !native_reference.available) {
    return comparison;
  }

  comparison.available = true;
  comparison.relative_delay_delta = RelativeDelta(numerical_result.selected_delay_ns, native_reference.delay_ns);
  comparison.relative_power_delta = RelativeDelta(numerical_result.selected_power_w, native_reference.power_w);
  comparison.delay_within_tolerance = comparison.relative_delay_delta <= delay_relative_tolerance;
  comparison.power_within_tolerance = comparison.relative_power_delta <= power_relative_tolerance;
  comparison.runtime_faster = native_reference.runtime_ms > 0.0 && numerical_result.runtime_ms > 0.0
                              && numerical_result.runtime_ms + numerical_htree::kEpsilon < native_reference.runtime_ms;
  return comparison;
}

}  // namespace icts
