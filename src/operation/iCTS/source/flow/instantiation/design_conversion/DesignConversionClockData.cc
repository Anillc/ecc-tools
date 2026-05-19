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
 * @file DesignConversionClockData.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief CTS clock-data materialization from SDC declarations.
 */

#include <glog/logging.h>

#include <cstddef>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sdc/ClockTraceResolver.hh"
#include "adapter/sdc/SdcClockModel.hh"
#include "adapter/sdc/SdcClockReader.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "instantiation/design_conversion/DesignConversion.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"

namespace icts {

auto DesignConversion::readClockData() -> bool
{
  std::string clock_source = "sdc";
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;

  const auto sdc_clock_data = SdcClockReader().readClockData();
  std::set<std::string> sdc_clock_names;
  std::set<std::string> traceable_sdc_clock_names;
  std::map<std::string, double> sdc_period_by_clock;
  std::map<std::string, bool> sdc_resolved_by_clock;
  for (const auto& clock_decl : sdc_clock_data.clocks) {
    if (clock_decl.clock_name.empty()) {
      continue;
    }
    sdc_clock_names.insert(clock_decl.clock_name);
    if (!clock_decl.is_virtual) {
      traceable_sdc_clock_names.insert(clock_decl.clock_name);
    }
    sdc_period_by_clock[clock_decl.clock_name] = clock_decl.period_ns;
    sdc_resolved_by_clock[clock_decl.clock_name] = clock_decl.period_resolved;
  }

  if (sdc_clock_names.empty()) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "DesignConversion",
                           "no SDC clocks were declared; CTS clock read will be an explicit no-op.", {{"clock_source", "sdc"}});
  }

  std::map<std::string, std::vector<std::string>> configured_nets_by_clock;
  std::size_t active_configured_mapping_count = 0U;
  bool preflight_failed = false;
  std::string failure_reason = "n/a";
  if (CONFIG_INST.is_use_netlist()) {
    for (const auto& [clock_name, net_name] : CONFIG_INST.get_net_list()) {
      if (!sdc_clock_names.contains(clock_name)) {
        preflight_failed = true;
        failure_reason = "configured_clock_not_declared_in_sdc";
        schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "DesignConversion",
                               "configured CTS clock net mapping is rejected because the clock is not declared in SDC.",
                               {{"clock", clock_name}, {"net", net_name}, {"clock_source", "sdc"}});
        LOG_ERROR << "DesignConversion: reject configured clock mapping for \"" << clock_name << "\" because it is not declared in SDC.";
        continue;
      }
      configured_nets_by_clock[clock_name].push_back(net_name);
    }

    for (const auto& [clock_name, net_names] : configured_nets_by_clock) {
      for (const auto& net_name : net_names) {
        clock_net_pairs.emplace_back(clock_name, net_name);
        ++active_configured_mapping_count;
      }
    }
    for (const auto& clock_name : sdc_clock_names) {
      if (configured_nets_by_clock.contains(clock_name)) {
        continue;
      }
      preflight_failed = true;
      if (failure_reason == "n/a") {
        failure_reason = "configured_clock_net_mapping_missing";
      }
      schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "DesignConversion",
                             "manual CTS clock net mode requires a configured net mapping for each SDC clock.",
                             {{"clock", clock_name}, {"clock_source", "sdc"}});
      LOG_ERROR << "DesignConversion: SDC clock \"" << clock_name << "\" has no configured clock net mapping while use_netlist is enabled.";
    }
  } else if (!sdc_clock_data.clocks.empty()) {
    const auto trace_result = WRAPPER_INST.traceSdcClocks(sdc_clock_data);
    clock_net_pairs = trace_result.clock_net_pairs;
    std::set<std::string> accepted_trace_clock_names;
    for (const auto& [clock_name, net_name] : clock_net_pairs) {
      (void) net_name;
      accepted_trace_clock_names.insert(clock_name);
    }
    for (const auto& clock_name : traceable_sdc_clock_names) {
      if (accepted_trace_clock_names.contains(clock_name)) {
        continue;
      }
      preflight_failed = true;
      if (failure_reason == "n/a") {
        failure_reason = "clock_trace_no_targets";
      }
      schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "DesignConversion",
                             "SDC clock tracing found no CTS target net for a traceable SDC clock.",
                             {{"clock", clock_name}, {"clock_source", "sdc"}});
      LOG_ERROR << "DesignConversion: SDC clock tracing found no CTS target net for \"" << clock_name << "\".";
    }
  }

  if (!preflight_failed && !clock_net_pairs.empty() && !WRAPPER_INST.readClocks(clock_net_pairs)) {
    preflight_failed = true;
    failure_reason = "clock_materialization_failed";
  }

  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    const auto period_iter = sdc_period_by_clock.find(clock->get_clock_name());
    const auto resolved_iter = sdc_resolved_by_clock.find(clock->get_clock_name());
    const bool period_resolved = resolved_iter == sdc_resolved_by_clock.end() || resolved_iter->second;
    if (period_iter != sdc_period_by_clock.end() && period_iter->second > 0.0 && period_resolved) {
      clock->set_clock_period_ns(period_iter->second);
      clock->set_clock_period_source("sdc");
    }
  }

  const auto materialized_clock_count = DESIGN_INST.get_clocks().size();
  if (!preflight_failed) {
    DESIGN_INST.emitClockDistributionSummary();
  }
  schema::KeyValueFields read_data_fields = {
      {"clock_source", clock_source},
      {"status", preflight_failed ? "failed" : "finished"},
  };
  if (preflight_failed && !failure_reason.empty()) {
    read_data_fields.emplace_back("failure_reason", failure_reason);
  }
  read_data_fields.insert(read_data_fields.end(), {
                                                      {"sdc_declared_clocks", std::to_string(sdc_clock_names.size())},
                                                      {"configured_clock_net_mappings", std::to_string(active_configured_mapping_count)},
                                                      {"added_clock_nets", std::to_string(materialized_clock_count)},
                                                      {"total_clock_nets", std::to_string(materialized_clock_count)},
                                                  });
  schema::EmitKeyValueTable("ReadData Overview", read_data_fields);
  if (preflight_failed) {
    DESIGN_INST.clearClocks();
    DESIGN_INST.clearTopologyObjects();
  }
  return !preflight_failed;
}

}  // namespace icts
