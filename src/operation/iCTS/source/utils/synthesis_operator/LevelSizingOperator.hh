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
 * @file LevelSizingOperator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <unordered_map>
#include <vector>

#include "SolverPipelineData.hh"
#include "SolverRuntimeContext.hh"

namespace icts {

class LevelSizingOperator
{
 public:
  explicit LevelSizingOperator(const SolverRuntimeContext& runtime) : _runtime(runtime) {}
  void run(SolverPipelineState& state) const;

 private:
  void enumerateLevelSizing(SolverPipelineState& state, size_t depth, size_t max_lib_index, std::vector<size_t>& current_assignment,
                            std::vector<SolverSizingCandidate>& feasible_candidates, std::vector<SolverSizingCandidate>& all_candidates,
                            SolverSizingSearchStats& stats) const;
  SolverSizingCandidate evaluateSizing(SolverPipelineState& state, const std::vector<size_t>& level_lib_indices,
                                       SolverSizingSearchStats& stats) const;
  SolverFeasibilityResult checkSizingFeasibility(const SolverPipelineState& state) const;
  void applyLevelSizing(SolverPipelineState& state, const std::vector<size_t>& level_lib_indices) const;
  void reevaluateTree(SolverPipelineState& state) const;
  void refreshNetEvaluationOrder(SolverPipelineState& state) const;
  int computeNetEvaluationOrder(const SolverPipelineState& state, Net* net, std::unordered_map<Net*, int>& cache) const;
  void normalizeCandidates(std::vector<SolverSizingCandidate>& candidates) const;
  static bool dominates(const SolverSizingCandidate& lhs, const SolverSizingCandidate& rhs);
  size_t selectBalancedCandidate(const std::vector<SolverSizingCandidate>& candidates) const;
  size_t countParetoCandidates(const std::vector<SolverSizingCandidate>& candidates) const;
  std::string formatFeasibilitySummary(const SolverFeasibilityResult& result) const;
  double totalBufferArea(const SolverPipelineState& state) const;
  double totalBufferPower(const SolverPipelineState& state) const;
  void logSizingSummary(const SolverPipelineState& state, const SolverSizingCandidate& candidate) const;

  const SolverRuntimeContext& _runtime;
};

}  // namespace icts
