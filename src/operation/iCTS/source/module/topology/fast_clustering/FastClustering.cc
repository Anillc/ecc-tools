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
 * @file FastClustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Fast spatial clustering facade for topology clustering.
 */

#include "FastClustering.hh"

#include <glog/logging.h>

#include <optional>
#include <ostream>
#include <vector>

#include "Clustering.hh"
#include "FastClusteringInternal.hh"
#include "LinearClustering.hh"
#include "Log.hh"
#include "TopologyConfig.hh"

namespace icts {

namespace detail = fast_clustering;

auto FastClustering::buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> LinearClusteringConfig
{
  return LinearClustering::buildElectricalBaseConfig(max_fanout, max_cap);
}

auto FastClustering::runDefault(const std::vector<Pin*>& loads, const LinearClusteringConfig& base_config) -> ClusterResult
{
  return run(loads, base_config);
}

auto FastClustering::run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }

  auto entries = detail::CollectEntries(loads);
  if (entries.empty()) {
    LOG_WARNING << "Fast clustering skipped: no valid load pins.";
    return result;
  }

  auto drafts = detail::BuildSpatialRecursiveClusters(entries, config);
  detail::PolishSmallClusters(drafts, entries, config);

  auto finalized = detail::FinalizeClusters(drafts, entries, config);
  if (!finalized.has_value() || detail::CountAssignedLoads(*finalized) != entries.size()) {
    LOG_WARNING << "Fast clustering failed to produce a legal complete partition. Falling back to linear clustering.";
    return LinearClustering::run(loads, config);
  }

  LOG_INFO << "Fast clustering done: loads=" << entries.size() << ", clusters=" << finalized->clusters.size()
           << ", strategy=recursive_spatial_bisect";
  return *finalized;
}

}  // namespace icts
