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
 * @file LinearClusteringSyntheticConfig.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Config and legality helpers for synthetic linear clustering support.
 */

#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "TopologyConfig.hh"
#include "clustering/Clustering.hh"
#include "common/linear_clustering/metrics/ClusterGeometrySupport.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic::detail {

auto ConfigureLinearDefaults(icts::LinearClusteringConfig& config, std::size_t max_fanout) -> void
{
  config.max_fanout = max_fanout;
  config.max_diameter = static_cast<int>(kDefaultMaxDiameter);
  config.enable_exact_cap = false;
}

auto ConfigureSyntheticFallbackCapNeutral(icts::LinearClusteringConfig& config) -> void
{
  config.enable_exact_cap = false;
  config.always_build_exact_cap = false;
  config.max_cap = std::numeric_limits<double>::infinity();
}

auto CaptureConstraintExpectation(const icts::LinearClusteringConfig& config, ClusteringInvocation& invocation) -> void
{
  invocation.used_linear_api = true;
  invocation.api_name = "Clustering::linearClustering(loads, config)";
  invocation.max_fanout = config.max_fanout;
  invocation.max_diameter = static_cast<double>(config.max_diameter);
  invocation.has_constraint_expectation = true;
}

auto MakeConstraintConfig(const ConstraintConfigSpec& spec) -> icts::LinearClusteringConfig
{
  icts::LinearClusteringConfig config{};
  ConfigureLinearDefaults(config, spec.max_fanout);
  ConfigureSyntheticFallbackCapNeutral(config);
  config.order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  config.split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  config.max_fanout = spec.max_fanout;
  config.max_diameter = static_cast<int>(std::lround(spec.max_diameter));
  return config;
}

auto ValidateClusterLegality(const icts::ClusterResult& result, const ClusteringInvocation& invocation, std::string& error) -> bool
{
  if (!invocation.has_constraint_expectation) {
    return true;
  }
  for (std::size_t cluster_id = 0; cluster_id < result.clusters.size(); ++cluster_id) {
    const auto& cluster = result.clusters.at(cluster_id);
    if (cluster.empty()) {
      continue;
    }
    if (invocation.max_fanout > 0 && cluster.size() > invocation.max_fanout) {
      std::ostringstream stream;
      stream << "fanout violation at cluster " << cluster_id << ": size=" << cluster.size() << ", max_fanout=" << invocation.max_fanout;
      error = stream.str();
      return false;
    }

    if (invocation.max_diameter > 0.0) {
      const auto diameter = static_cast<double>(common::linear_clustering::CalcClusterDiameter(cluster));
      if (diameter > invocation.max_diameter) {
        std::ostringstream stream;
        stream << "diameter violation at cluster " << cluster_id << ": diameter=" << diameter
               << ", max_diameter=" << invocation.max_diameter;
        error = stream.str();
        return false;
      }
    }
  }
  return true;
}

}  // namespace icts_test::linear_clustering::synthetic::detail
