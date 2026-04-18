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
 * @file ConstraintEvaluator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Cluster legality evaluation for linear clustering.
 */

#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "LinearClusteringTypes.hh"

namespace icts {

struct LinearClusteringConfig;
class Pin;
template <typename T>
class Point;

class ConstraintEvaluator
{
 public:
  ConstraintEvaluator() = default;
  ~ConstraintEvaluator() = default;

  auto evaluate(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment, const LinearClusteringConfig& config,
                bool need_exact_cap) -> ConstraintEvaluation;
  auto evaluateLoads(const std::vector<Pin*>& loads, const Point<int>& routing_root, const LinearClusteringConfig& config,
                     bool need_exact_cap) -> ConstraintEvaluation;

 private:
  static auto calcSpan(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment) -> ClusterSpanMetrics;
  static auto collectPins(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment) -> std::vector<Pin*>;
  auto evaluatePinnedLoads(const std::vector<Pin*>& loads, std::size_t fanout, int diameter, const Point<int>& routing_root,
                           const LinearClusteringConfig& config, bool need_exact_cap) -> ConstraintEvaluation;
  auto estimatePinCap(const std::vector<Pin*>& loads) -> double;
  auto estimateExactCap(const std::vector<Pin*>& loads, const Point<int>& synthetic_root, const LinearClusteringConfig& config)
      -> ElectricalEstimate;
  auto queryPinCap(const Pin* pin) -> double;

  std::unordered_map<const Pin*, double> _pin_cap_cache;
};

}  // namespace icts
