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
 * @file SkewPostOptimizationOperator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "SolverPipelineData.hh"
#include "SolverRuntimeContext.hh"

namespace icts {

class Node;
class Inst;
class Pin;

class SkewPostOptimizationOperator
{
 public:
  explicit SkewPostOptimizationOperator(const SolverRuntimeContext& runtime) : _runtime(runtime) {}
  void run(SolverPipelineState& state) const;

 private:
  struct WorstSinkPair
  {
    Pin* fast_sink = nullptr;
    Pin* slow_sink = nullptr;
    double fast_delay = 0.0;
    double slow_delay = 0.0;

    bool valid() const { return fast_sink != nullptr && slow_sink != nullptr && slow_delay >= fast_delay; }
  };

  struct MoveCandidate
  {
    struct ParentCapStatus
    {
      std::string net_name;
      double cap_load = 0.0;
      double overflow = 0.0;
      double limit = 0.0;
    };

    Inst* inst = nullptr;
    std::string from_master;
    size_t target_lib_index = 0;
    double skew = 0.0;
    double area = 0.0;
    double power = 0.0;
    ParentCapStatus baseline_parent_cap;
    ParentCapStatus updated_parent_cap;
  };

  struct SearchStats
  {
    size_t iterations = 0;
    size_t evaluated_moves = 0;
    size_t accepted_moves = 0;
    size_t parent_cap_rejected_moves = 0;
    size_t worsening_moves = 0;
  };

  using ParentCapStatus = MoveCandidate::ParentCapStatus;

  void reevaluateTree(SolverPipelineState& state) const;
  void refreshNetEvaluationOrder(SolverPipelineState& state) const;
  int computeNetEvaluationOrder(const SolverPipelineState& state, Net* net, std::unordered_map<Net*, int>& cache) const;

  WorstSinkPair extractWorstPair(Pin* root) const;
  Node* computeLca(Node* lhs, Node* rhs) const;
  std::vector<Inst*> collectCandidateBuffers(const SolverPipelineState& state, Pin* slow_sink, Node* lca) const;
  std::optional<ParentCapStatus> measureParentCapStatus(const SolverPipelineState& state, const Inst* inst) const;
  bool parentCapNonWorsening(const ParentCapStatus& baseline, const ParentCapStatus& updated) const;
  std::optional<MoveCandidate> findBestMove(SolverPipelineState& state, const std::vector<Inst*>& candidates, double baseline_skew,
                                            const std::unordered_map<std::string, size_t>& lib_index_lookup, SearchStats& stats) const;

  std::string formatParentCapStatus(const ParentCapStatus& status) const;
  double currentGlobalSkew(const SolverPipelineState& state) const;
  double totalBufferArea(const SolverPipelineState& state) const;
  double totalBufferPower(const SolverPipelineState& state) const;
  void logSummary(const SolverPipelineState& state, const SearchStats& stats, double initial_skew) const;

  const SolverRuntimeContext& _runtime;
};

}  // namespace icts
