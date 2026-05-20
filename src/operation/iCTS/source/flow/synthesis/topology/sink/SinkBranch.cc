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

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "Net.hh"
#include "config/Config.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/HTreeSynthesisOptions.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "synthesis/topology/sink/SinkLoadClustering.hh"
#include "synthesis/trace/distance/TopologyDistanceReport.hh"
#include "synthesis/trace/topology_result/TopologyResult.hh"

namespace icts::topology {
namespace {

auto ApplyMinTopInputSlew(HTree::BuildOptions& htree_options) -> void
{
  htree_options.min_top_input_slew_ns = CONFIG_INST.get_root_input_slew();
}

auto BuildSinkHtreeOptions(bool enable_sink_clustering, const Topology::BuildOptions& options) -> HTree::BuildOptions
{
  HTree::BuildOptions htree_options;
  ApplyMinTopInputSlew(htree_options);
  htree_options.topology_loads_are_local_buffers = enable_sink_clustering;
  htree_options.characterization_library = options.characterization_library;
  htree_options.additional_characterization_lengths_um = options.additional_characterization_lengths_um;
  htree_options.clock_period_ns = options.clock_period_ns;
  htree_options.clock_period_source = options.clock_period_source;
  htree_options.log_context = options.log_context;
  htree_options.object_name_prefix = options.object_name_prefix;
  htree_options.enable_analytical_solver = CONFIG_INST.is_enable_analytical_htree();
  return htree_options;
}

auto DetailStageReportOptions() -> schema::StageReportOptions
{
  return schema::StageReportOptions{.context_sink = schema::ReportSink::kDetail, .summary_sink = schema::ReportSink::kDetail};
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

  SinkTreeLoadPreparation sink_preparation;
  {
    auto preparation_stage = SCHEMA_WRITER_INST.beginStage(
        "Topology", "Prepare sink loads",
        {
            {"root_loads", std::to_string(root_loads.size())},
            {"sink_clustering_enabled",
             options.enable_sink_clustering.value_or(CONFIG_INST.is_enable_sink_clustering()) ? "true" : "false"},
        },
        DetailStageReportOptions());
    sink_preparation = PrepareSinkTreeLoads(result, root_loads, options);
    if (sink_preparation.success) {
      preparation_stage.finished({
          {"htree_sinks", std::to_string(sink_preparation.htree_sinks.size())},
          {"cluster_buffers", std::to_string(result.cluster_buffers.size())},
      });
    } else {
      preparation_stage.failed({{"reason", result.failure_reason.empty() ? "sink_load_preparation_failed" : result.failure_reason}});
    }
  }
  if (!sink_preparation.success) {
    root_net_side_effects.restore();
    return result;
  }
  if (result.sink_clustering_enabled) {
    ConnectNet(root_net, root_driver, sink_preparation.htree_sinks);
  }

  auto htree_options = BuildSinkHtreeOptions(result.sink_clustering_enabled, options);
  {
    auto htree_stage = SCHEMA_WRITER_INST.beginStage("Topology", "Build downstream HTree",
                                                     {
                                                         {"htree_sinks", std::to_string(sink_preparation.htree_sinks.size())},
                                                         {"sink_clustering_enabled", result.sink_clustering_enabled ? "true" : "false"},
                                                     },
                                                     DetailStageReportOptions());
    result.htree_result = HTree::build(root_net, htree_options);
    if (result.htree_result.success) {
      htree_stage.finished({
          {"selected_depth", std::to_string(result.htree_result.selected_depth.value_or(0U))},
          {"inserted_insts", std::to_string(result.htree_result.inserted_insts.size())},
          {"inserted_nets", std::to_string(result.htree_result.inserted_nets.size())},
      });
    } else {
      htree_stage.failed(
          {{"reason", result.htree_result.failure_reason.empty() ? "unknown_h_tree_failure" : result.htree_result.failure_reason}});
    }
  }
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
    auto distance_stage = SCHEMA_WRITER_INST.beginStage("Topology", "Emit cluster leaf distance report",
                                                        {
                                                            {"cluster_buffers", std::to_string(result.cluster_buffers.size())},
                                                        },
                                                        DetailStageReportOptions());
    result.cluster_leaf_distance_summary = EmitClusterLeafDistanceTables(result);
    if (result.cluster_leaf_distance_summary.has_value()) {
      distance_stage.finished({
          {"cluster_leaf_pairs", std::to_string(result.cluster_leaf_distance_summary->count)},
          {"mean_distance_um", std::to_string(result.cluster_leaf_distance_summary->mean_distance_um)},
      });
    } else {
      distance_stage.skip({{"reason", "no_cluster_leaf_distance_summary"}});
    }
  }
  result.success = true;
  return result;
}

}  // namespace icts::topology
