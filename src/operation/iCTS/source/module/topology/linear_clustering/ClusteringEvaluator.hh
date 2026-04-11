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
 * @file ClusteringEvaluator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Cost evaluation for legal linear clustering partitions.
 */

#pragma once

#include <vector>

#include "ConstraintEvaluator.hh"
#include "LinearClusteringTypes.hh"

namespace icts {

struct LinearClusteringConfig;

class ClusteringEvaluator
{
 public:
  ClusteringEvaluator() = default;
  ~ClusteringEvaluator() = default;

  auto evaluateSegment(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment, const LinearClusteringConfig& config,
                       bool need_exact_cap) -> ClusterScoreBreakdown;
  auto evaluatePartition(const std::vector<OrderedLoad>& ordered_loads, const std::vector<SegmentRange>& segments,
                         const LinearClusteringConfig& config, bool need_exact_cap) -> PartitionScore;

 private:
  ConstraintEvaluator _constraint_evaluator;
};

}  // namespace icts
