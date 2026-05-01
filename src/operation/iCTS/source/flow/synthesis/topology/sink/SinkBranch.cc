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
 * @file SinkBranch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements downstream sink-tree Topology coordination.
 */

#include "synthesis/topology/sink/SinkBranch.hh"

#include <string>

#include "Log.hh"
#include "config/Config.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "synthesis/topology/sink/SinkLoadClustering.hh"
#include "synthesis/trace/distance/TopologyDistanceReport.hh"
#include "synthesis/trace/topology_result/TopologyResult.hh"

namespace icts::topology {
namespace {

auto ApplyMinTopInputSlew(HTree::BuildOptions& htree_options) -> void
{
  const double max_buf_tran = CONFIG_INST.get_max_buf_tran();
  if (max_buf_tran > 0.0) {
    htree_options.min_top_input_slew_ns = max_buf_tran * 0.5;
  }
}

auto BuildSinkHtreeOptions(bool enable_sink_clustering, const Topology::BuildOptions& options) -> HTree::BuildOptions
{
  HTree::BuildOptions htree_options;
  ApplyMinTopInputSlew(htree_options);
  htree_options.topology_loads_are_local_buffers = enable_sink_clustering;
  htree_options.characterization_library = options.characterization_library;
  htree_options.additional_characterization_lengths_um = options.additional_characterization_lengths_um;
  htree_options.log_context = options.log_context;
  htree_options.object_name_prefix = options.object_name_prefix;
  return htree_options;
}

}  // namespace

auto BuildSinkTree(Net& root_net, const Topology::BuildOptions& options) -> Topology::BuildResult
{
  Topology::BuildResult result;
  auto* root_driver = root_net.get_driver();
  if (root_driver == nullptr) {
    LOG_ERROR << "Topology: root net \"" << root_net.get_name() << "\" driver is null.";
    result.failure_reason = "root net driver is null";
    return result;
  }

  const auto root_loads = CollectValidLoads(root_net);
  if (root_loads.empty()) {
    LOG_WARNING << "Topology: root net \"" << root_net.get_name() << "\" has no loads.";
    result.failure_reason = "root net load list is empty";
    return result;
  }

  RootNetSideEffectGuard root_net_side_effects(root_net, root_driver);
  ConnectNet(root_net, root_driver, root_loads);

  const auto sink_preparation = PrepareSinkTreeLoads(result, root_loads, options);
  if (!sink_preparation.success) {
    root_net_side_effects.restore();
    return result;
  }
  if (result.sink_clustering_enabled) {
    ConnectNet(root_net, root_driver, sink_preparation.htree_sinks);
  }

  auto htree_options = BuildSinkHtreeOptions(result.sink_clustering_enabled, options);
  result.htree_result = HTree::build(root_net, htree_options);
  if (!result.htree_result.success) {
    const std::string htree_failure
        = result.htree_result.failure_reason.empty() ? "unknown H-tree failure" : result.htree_result.failure_reason;
    LOG_ERROR << "Topology: H-tree build failed: " << htree_failure;
    result.failure_reason = "H-tree build failed: " + htree_failure;
    root_net_side_effects.restore();
    return result;
  }

  RecordSinkHtreeResult(result);
  if (result.htree_result.root_net != &root_net) {
    LOG_ERROR << "Topology: H-tree result root net does not match input root net \"" << root_net.get_name() << "\".";
    result.failure_reason = "H-tree result root net mismatch";
    root_net_side_effects.restore();
    return result;
  }

  if (result.sink_clustering_enabled) {
    result.cluster_leaf_distance_summary = EmitClusterLeafDistanceTables(result);
  }
  result.success = true;
  return result;
}

}  // namespace icts::topology
