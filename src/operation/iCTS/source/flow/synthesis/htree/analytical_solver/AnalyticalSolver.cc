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

#include "synthesis/htree/analytical_solver/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh"

namespace icts::htree::analytical_solver {

auto SolveAnalyticalHTreeCandidates(const AnalyticalSolverRequest& request) -> AnalyticalSolverResult
{
  const auto validation_failure = ValidateRequest(request);
  if (!validation_failure.empty()) {
    return MakeFailure(validation_failure);
  }

  AnalyticalSolverResult result;
  RecordDiagnosticLibraryHits(request, result);
  result.candidates = BuildBeamCandidates(request, result);
  if (result.candidates.empty()) {
    result.success = false;
    if (result.root_fanout_rejected_count > 0U) {
      result.failure_reason = "root_fanout_illegal";
    } else if (result.lattice_rejected_count > 0U) {
      result.failure_reason = "materialized_char_out_of_lattice";
    } else {
      result.failure_reason = "no_analytical_candidate";
    }
    return result;
  }

  result.success = true;
  return result;
}

}  // namespace icts::htree::analytical_solver
