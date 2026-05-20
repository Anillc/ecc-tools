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
 * @file TopologyResult.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements clock-tree synthesis metrics and temporary ownership transfers.
 */

#include "synthesis/trace/topology_result/TopologyResult.hh"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/HTreeSynthesisResult.hh"

namespace icts::topology {
namespace {

template <typename T>
auto absorbOwnedObjects(std::vector<std::unique_ptr<T>>& target, std::vector<std::unique_ptr<T>>& source) -> void
{
  target.reserve(target.size() + source.size());
  for (auto& object : source) {
    target.push_back(std::move(object));
  }
  source.clear();
}

auto absorbHtreeInsertedObjects(Topology::BuildResult& result) -> void
{
  result.inserted_inst_levels.insert(result.inserted_inst_levels.end(), result.htree_result.inserted_inst_levels.begin(),
                                     result.htree_result.inserted_inst_levels.end());
  result.inserted_net_levels.insert(result.inserted_net_levels.end(), result.htree_result.inserted_net_levels.begin(),
                                    result.htree_result.inserted_net_levels.end());
  absorbOwnedObjects(result.inserted_insts, result.htree_result.inserted_insts);
  absorbOwnedObjects(result.inserted_pins, result.htree_result.inserted_pins);
  absorbOwnedObjects(result.inserted_nets, result.htree_result.inserted_nets);
}

auto absorbHtreeInsertedObjects(Topology::SourceTrunkBuildResult& result) -> void
{
  result.inserted_inst_levels.insert(result.inserted_inst_levels.end(), result.htree_result.inserted_inst_levels.begin(),
                                     result.htree_result.inserted_inst_levels.end());
  result.inserted_net_levels.insert(result.inserted_net_levels.end(), result.htree_result.inserted_net_levels.begin(),
                                    result.htree_result.inserted_net_levels.end());
  absorbOwnedObjects(result.inserted_insts, result.htree_result.inserted_insts);
  absorbOwnedObjects(result.inserted_pins, result.htree_result.inserted_pins);
  absorbOwnedObjects(result.inserted_nets, result.htree_result.inserted_nets);
}

auto absorbSegmentInsertedObjects(Topology::SourceTrunkBuildResult& result, SourceTrunkSegment::BuildResult& segment_result) -> void
{
  result.inserted_inst_levels.insert(result.inserted_inst_levels.end(), segment_result.inserted_inst_levels.begin(),
                                     segment_result.inserted_inst_levels.end());
  result.inserted_net_levels.insert(result.inserted_net_levels.end(), segment_result.inserted_net_levels.begin(),
                                    segment_result.inserted_net_levels.end());
  absorbOwnedObjects(result.inserted_insts, segment_result.inserted_insts);
  absorbOwnedObjects(result.inserted_pins, segment_result.inserted_pins);
  absorbOwnedObjects(result.inserted_nets, segment_result.inserted_nets);
}

auto selectedLeafTopologyLevel(const HTree::BuildResult& htree_result) -> int
{
  if (htree_result.selected_depth.has_value()) {
    return static_cast<int>(*htree_result.selected_depth);
  }
  return htree_result.levels.empty() ? 0 : static_cast<int>(htree_result.levels.size());
}

auto recordClusterSinkNetLevels(Topology::BuildResult& result) -> void
{
  const int topology_level = selectedLeafTopologyLevel(result.htree_result);
  std::size_t index_in_level = 0U;
  for (const auto& cluster_buffer : result.cluster_buffers) {
    if (cluster_buffer.sink_net == nullptr) {
      continue;
    }
    result.inserted_net_levels.push_back(HTree::InsertedNetLevel{
        .net = cluster_buffer.sink_net,
        .topology_level = topology_level,
        .index_in_level = index_in_level++,
    });
  }
}

}  // namespace

auto RecordSinkHtreeResult(Topology::BuildResult& result) -> void
{
  result.selected_htree_level_count = result.htree_result.levels.size();
  result.selected_htree_depth = result.htree_result.selected_depth;
  result.htree_inserted_buffer_count = result.htree_result.inserted_insts.size();
  result.htree_inserted_net_count = result.htree_result.inserted_nets.size();
  recordClusterSinkNetLevels(result);
  absorbHtreeInsertedObjects(result);
}

auto RecordTopSegmentResult(Topology::SourceTrunkBuildResult& result, SourceTrunkSegment::BuildResult& segment_result) -> void
{
  result.used_boundary_relaxation = segment_result.used_boundary_relaxation;
  result.inserted_buffer_count = segment_result.inserted_insts.size();
  result.inserted_net_count = segment_result.inserted_nets.size();
  absorbSegmentInsertedObjects(result, segment_result);
  result.success = true;
}

auto RecordTopHtreeResult(Topology::SourceTrunkBuildResult& result) -> void
{
  result.used_boundary_relaxation = result.htree_result.used_boundary_relaxation;
  result.inserted_buffer_count = result.htree_result.inserted_insts.size();
  result.inserted_net_count = result.htree_result.inserted_nets.size();
  absorbHtreeInsertedObjects(result);
  result.success = true;
}

}  // namespace icts::topology
