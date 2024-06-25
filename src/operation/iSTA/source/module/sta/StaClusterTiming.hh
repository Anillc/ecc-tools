// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file StaClusterTiming.hh
 * @author shy long (longshy@pcl.ac.cn)
 * @brief The class for macro place cluster timing analysis.
 * @version 0.1
 * @date 2024-05-31
 */

#pragma once
#include <set>
#include <vector>

#include "StaFunc.hh"

namespace ista {

/**
 * @brief marco place cluster timing analysis.
 *
 */
class StaClusterTiming : public StaFunc {
 public:
  StaClusterTiming(std::vector<std::set<std::string>> cluster_instances)
      : _cluster_instances(cluster_instances) {}
  ~StaClusterTiming() = default;

  void addHierSubNetlist();
  void buildSubnetlistToInst();
  void addRemainingInstances(Instance&& instance) {
    _remaining_instances.emplace_back(std::move(instance));
  }

 private:
  void addPortForSubnetlist(Instance& inst, Netlist& subnetlist);
  bool isBoundaryInstance(
      Instance& inst, std::set<std::string> instance_own_cluster,
      const std::vector<std::set<std::string>>& cluster_instances);
  void addPortForBoundaryInstance(
      Instance& boundary_inst, std::set<std::string> boundary_inst_own_cluster,
      Netlist& subnetlist);
  std::vector<std::set<std::string>> _cluster_instances;
  std::list<Instance>
      _remaining_instances;  // collection of the cluster's instance,where the
                             // cluster only has one instnce.
};

}  // namespace ista