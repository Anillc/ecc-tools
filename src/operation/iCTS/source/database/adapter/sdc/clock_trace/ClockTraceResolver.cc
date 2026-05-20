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
 * @file ClockTraceResolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief SDC-rooted CTS clock trace resolver implementation.
 */

#include "ClockTraceResolver.hh"

#include <glog/logging.h>

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "Log.hh"
#include "SdcClockReader.hh"
#include "clock_trace/SdcClockTraceAlgorithm.hh"
#include "idm.h"
#include "logger/Schema.hh"

namespace icts {

auto ClockTraceResolver::resolve(const SdcClockData& clock_data) -> ClockTraceResult
{
  return resolve(clock_data, dmInst->get_idb_design());
}

auto ClockTraceResolver::resolve(const SdcClockData& clock_data, idb::IdbDesign* idb_design) -> ClockTraceResult
{
  ClockTraceResult result;
  if (idb_design == nullptr || idb_design->get_net_list() == nullptr) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "ClockTraceResolver", "clock tracing skipped because iDB design is not ready.",
                           {{"clock_source", "sdc"}});
    LOG_ERROR << "ClockTraceResolver: iDB design or net list is null.";
    return result;
  }

  const auto case_constraints = clock_trace::BuildCaseConstraintSet(clock_data);
  const auto generated_boundary_owner_by_net = clock_trace::BuildGeneratedBoundaryOwners(idb_design, clock_data);
  const auto clock_view_by_name = clock_trace::BuildClockDeclViews(idb_design, clock_data);

  std::vector<ClockTraceRecord> candidate_records;
  for (const auto& clock : clock_data.clocks) {
    auto records = clock_trace::TraceClock(idb_design, clock, case_constraints, generated_boundary_owner_by_net);
    candidate_records.insert(candidate_records.end(), records.begin(), records.end());
  }

  std::map<std::string, std::set<std::string>> accepted_clock_names_by_net;
  std::map<std::string, bool> has_strong_target_by_clock;
  const auto sink_threshold = clock_trace::StrongTargetSinkThreshold();
  for (const auto& record : candidate_records) {
    if (record.status == "accepted" && !record.net_name.empty()) {
      accepted_clock_names_by_net[record.net_name].insert(record.clock_name);
      has_strong_target_by_clock[record.clock_name]
          = has_strong_target_by_clock[record.clock_name] || clock_trace::IsStrongClockTarget(record, sink_threshold);
    }
  }

  std::set<std::pair<std::string, std::string>> emitted_pairs;
  for (auto record : candidate_records) {
    if (record.status == "accepted" && has_strong_target_by_clock[record.clock_name]
        && !clock_trace::IsStrongClockTarget(record, sink_threshold)) {
      record.status = "trace_stop";
      record.reason = "source_side_clock_sinks_below_target_threshold";
    }
    if (record.status == "accepted" && accepted_clock_names_by_net[record.net_name].size() > 1U) {
      record.status = "ambiguous";
      record.reason = "target_net_reachable_from_multiple_sdc_clocks";
    }
    if (record.status == "accepted" && emitted_pairs.insert({record.clock_name, record.net_name}).second) {
      result.clock_net_pairs.emplace_back(record.clock_name, record.net_name);
    }
    clock_trace::AnnotateRecordOwnership(record, clock_view_by_name);
    result.records.push_back(std::move(record));
  }

  result.unowned_clock_like_records = clock_trace::CollectUnownedClockLikeRecords(idb_design, result.records);
  clock_trace::EmitClockTraceReport(result.records);
  clock_trace::EmitSdcClockOwnershipReport(clock_data, clock_view_by_name, result.records);
  clock_trace::EmitUnownedClockLikeNetReport(result.unowned_clock_like_records);
  LOG_INFO << "ClockTraceResolver: accepted " << result.clock_net_pairs.size() << " CTS clock target net(s) from "
           << clock_data.clocks.size() << " SDC clock declaration(s); reported " << result.unowned_clock_like_records.size()
           << " unowned clock-like net(s).";
  return result;
}

}  // namespace icts
