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
 * @file TopologyConfig.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-24
 * @brief Shared configuration types for topology module algorithms.
 */

#pragma once

#include <cstddef>
#include <limits>

namespace icts {

struct BiPartitionConfig
{
  double max_ratio = 0.6;
  int max_iter = 10;
  int converge_threshold = 1000;
  std::size_t kmeans_iter_count = 5;
};

enum class LinearOrderStrategy
{
  kContinuousHilbert,
  kDiscreteHilbert,
  kDensityScaledContinuousHilbert,
  kDensityScaledDiscreteHilbert,
};

enum class LinearSplitStrategy
{
  kForwardGreedy,
  kReverseGreedy,
  kBidirectionalGreedy,
};

enum class LinearSweepMode
{
  kPrefixSweep,
  kStridedSweep,
  kPrefixAndStridedSweep,
};

enum class LinearRouterKind
{
  kFlute,
  kSalt,
  kBst,
  kCbs,
};

enum class LinearRootPolicy
{
  kMedian,
  kCenter,
};

enum class LinearScoringStrategy
{
  kMaxDiameter,      // Sum of per-cluster max internal diameter (default)
  kTotalWirelength,  // Sum of per-cluster routed wirelength
};

struct LinearClusteringConfig
{
  std::size_t max_fanout = 32;
  int max_diameter = std::numeric_limits<int>::max();
  double max_cap = std::numeric_limits<double>::infinity();

  LinearOrderStrategy order_strategy = LinearOrderStrategy::kContinuousHilbert;
  LinearSplitStrategy split_strategy = LinearSplitStrategy::kBidirectionalGreedy;
  LinearRouterKind router_kind = LinearRouterKind::kFlute;
  LinearRootPolicy root_policy = LinearRootPolicy::kMedian;
  LinearScoringStrategy scoring_strategy = LinearScoringStrategy::kMaxDiameter;
  LinearSweepMode sweep_mode = LinearSweepMode::kPrefixSweep;

  bool enable_exact_cap = true;
  bool always_build_exact_cap = false;
  // Strided sweep samples this many distinct cyclic start offsets over the full ring.
  // Prefix sweep count is derived from max_fanout and clamped to load_count.
  std::size_t strided_sweep_count = 2;

  int order_bits = 12;
  std::size_t density_grid_size = 8;
  double density_scale_power = 0.5;

  double wirelength_weight = 1.0;

  int routing_layer = 0;
  double wire_width = 0.0;
};

}  // namespace icts
