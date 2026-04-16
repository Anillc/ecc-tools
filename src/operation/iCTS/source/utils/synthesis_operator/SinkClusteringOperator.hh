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
 * @file SinkClusteringOperator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include "SolverPipelineData.hh"
#include "SolverRuntimeContext.hh"

namespace icts {

class SinkClusteringOperator
{
 public:
  explicit SinkClusteringOperator(const SolverRuntimeContext& runtime) : _runtime(runtime) {}
  bool run(SolverPipelineState& state) const;

 private:
  void init(SolverPipelineState& state) const;
  void buildLeafBuffers(SolverPipelineState& state) const;

  const SolverRuntimeContext& _runtime;
};

}  // namespace icts
