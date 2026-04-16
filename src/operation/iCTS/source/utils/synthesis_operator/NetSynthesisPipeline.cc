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
 * @file NetSynthesisPipeline.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "NetSynthesisPipeline.hh"

#include <ranges>
#include <sstream>

#include "log/Log.hh"

namespace icts {

namespace {
std::string formatBufferDepthSummary(const std::vector<std::vector<Inst*>>& buffers_by_depth)
{
  std::ostringstream oss;
  bool first = true;
  for (size_t depth = 0; depth < buffers_by_depth.size(); ++depth) {
    if (buffers_by_depth[depth].empty()) {
      continue;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << "d" << depth << "=" << buffers_by_depth[depth].size();
  }
  return first ? "none" : oss.str();
}
}  // namespace

NetSynthesisPipeline::NetSynthesisPipeline(const SolverRuntimeContext& runtime)
    : _runtime(runtime),
      _sink_clustering(runtime),
      _topology_builder(runtime),
      _long_wire_buffering(runtime),
      _level_sizing(runtime),
      _skew_post_opt(runtime)
{
}

void NetSynthesisPipeline::run(SolverPipelineState& state) const
{
  state.resetGeneratedData();
  if (!_sink_clustering.run(state)) {
    return;
  }

  _topology_builder.run(state);
  _long_wire_buffering.run(state);
  logTopologySummary(state);
  _level_sizing.run(state);
  _skew_post_opt.run(state);
}

void NetSynthesisPipeline::logTopologySummary(const SolverPipelineState& state) const
{
  const auto depth_summary = formatBufferDepthSummary(state.buffers_by_depth);
  std::ostringstream topology_summary;
  topology_summary << "Net [" << state.net_name << "] H-tree summary => depth: " << (state.max_depth + 1)
                   << ", inserted buffers: " << state.buffer_depths.size() << ", solver nets: " << state.nets.size() << ", depth buffers: ["
                   << depth_summary << "]";
  LOG_INFO << topology_summary.str();
  _runtime.saveToLog(topology_summary.str());
}

}  // namespace icts
