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
 * @file AnalyticalSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Analytical H-tree shortlist solver implementation.
 */

#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"

#include <string>
#include <vector>

#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalHTreeCandidateSearch.hh"

namespace icts::htree::analytical_solver {

auto SolveAnalyticalHTreeCandidates(const AnalyticalHTreeSolveProblem& solve_problem) -> AnalyticalSolverBuild
{
  const auto validation_failure = ValidateSolveProblem(solve_problem);
  if (!validation_failure.empty()) {
    return MakeFailure(validation_failure);
  }

  AnalyticalSolverBuild result;
  RecordDiagnosticLibraryHits(solve_problem, result);
  result.output.candidates = BuildBeamCandidates(solve_problem, result);
  if (result.output.candidates.empty()) {
    result.summary.success = false;
    if (result.summary.root_fanout_rejected_count > 0U) {
      result.summary.failure_reason = "root_fanout_illegal";
    } else if (result.summary.lattice_rejected_count > 0U) {
      result.summary.failure_reason = "materialized_char_out_of_lattice";
    } else {
      result.summary.failure_reason = "no_analytical_candidate";
    }
    return result;
  }

  result.summary.success = true;
  return result;
}

}  // namespace icts::htree::analytical_solver
