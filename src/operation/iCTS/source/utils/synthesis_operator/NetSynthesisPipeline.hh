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
 * @file NetSynthesisPipeline.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include "LevelSizingOperator.hh"
#include "LongWireBufferingOperator.hh"
#include "SinkClusteringOperator.hh"
#include "SkewPostOptimizationOperator.hh"
#include "TopologyBuilderOperator.hh"

namespace icts {

class NetSynthesisPipeline
{
 public:
  explicit NetSynthesisPipeline(const SolverRuntimeContext& runtime);
  void run(SolverPipelineState& state) const;

 private:
  void logTopologySummary(const SolverPipelineState& state) const;

  const SolverRuntimeContext& _runtime;
  SinkClusteringOperator _sink_clustering;
  TopologyBuilderOperator _topology_builder;
  LongWireBufferingOperator _long_wire_buffering;
  LevelSizingOperator _level_sizing;
  SkewPostOptimizationOperator _skew_post_opt;
};

}  // namespace icts
