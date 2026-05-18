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
 * @file AnalyticalValidation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Native legality validation bridge for analytical H-tree candidates.
 */

#include "synthesis/htree/analytical_solver/AnalyticalValidation.hh"

#include <optional>
#include <string>
#include <utility>

#include "HTreeTopologyChar.hh"
#include "synthesis/htree/analytical_solver/AnalyticalCandidate.hh"

namespace icts::htree::analytical_solver {
namespace {

auto MakeValidationFailure(std::string reason) -> AnalyticalValidationResult
{
  AnalyticalValidationResult result;
  result.legal = false;
  result.failure_reason = std::move(reason);
  return result;
}

auto ValidateRequest(const AnalyticalValidationRequest& request) -> std::string
{
  if (request.topology == nullptr) {
    return "missing_topology";
  }
  if (request.segment_pattern_library == nullptr) {
    return "missing_segment_pattern_library";
  }
  if (request.validate_sink_load_region && request.sink_load_region_legality_context == nullptr) {
    return "missing_sink_load_region_legality_context";
  }
  if (request.validate_root_driver_compensation && request.root_driver_compensation_pass == nullptr) {
    return "missing_root_driver_compensation_pass";
  }
  return {};
}

}  // namespace

auto ValidateAnalyticalCandidate(AnalyticalCandidate& candidate, const AnalyticalValidationRequest& request) -> AnalyticalValidationResult
{
  const auto request_failure = ValidateRequest(request);
  if (!request_failure.empty()) {
    return MakeValidationFailure(request_failure);
  }
  if (!candidate.isValid() || !candidate.materialized_char.has_value()) {
    return MakeValidationFailure(candidate.rejection_reason.empty() ? "invalid_analytical_candidate" : candidate.rejection_reason);
  }

  AnalyticalValidationResult result;
  if (request.validate_sink_load_region) {
    result.sink_load_region = ResolveSinkLoadRegionLegality(*request.topology, candidate.materialized_char->get_pattern_id(),
                                                            candidate.topology_pattern_library, *request.segment_pattern_library,
                                                            *request.sink_load_region_legality_context);
    if (!result.sink_load_region.legal) {
      result.failure_reason
          = result.sink_load_region.failure_reason.empty() ? "sink_load_region_illegal" : result.sink_load_region.failure_reason;
      return result;
    }
  }

  if (request.validate_root_driver_compensation) {
    result.root_driver_compensation
        = request.root_driver_compensation_pass->evaluate(candidate.materialized_char->get_pattern_id(), candidate.topology_pattern_library,
                                                          *request.segment_pattern_library, *request.topology);
    if (!result.root_driver_compensation.valid) {
      result.failure_reason = "root_driver_compensation_invalid";
      return result;
    }
  }

  result.legal = true;
  return result;
}

}  // namespace icts::htree::analytical_solver
