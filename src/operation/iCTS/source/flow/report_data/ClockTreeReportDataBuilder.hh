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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ClockTreeReportDataBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS clock-tree report-data construction helper.
 */

#pragma once

#include <cstddef>
#include <vector>

#include "report_data/ClockTreeReportData.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {

class Clock;
class Inst;
class Net;
class Pin;

struct ClockSinkDomainReportTopology
{
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  const Inst* root_buffer = nullptr;
  const Net* downstream_net = nullptr;
};

class ClockTreeReportDataBuilder
{
 public:
  ClockTreeReportDataBuilder() = delete;

  static auto appendSinkInsts(ClockTreeReportData& report_data, const Clock& clock, std::size_t clock_index, const std::vector<Pin*>& sinks,
                              CTSSinkDomain sink_domain) -> void;
  static auto appendDirectSinkDomain(ClockTreeReportData& report_data, const Clock& clock, std::size_t clock_index,
                                     const ClockSinkDomainReportTopology& sink_domain_topology) -> void;
  static auto makeSinkDomainReportData(const Clock& clock, std::size_t clock_index,
                                       const ClockSinkDomainReportTopology& sink_domain_topology, const ClockSynthesis::BuildResult& result)
      -> ClockTreeReportData;
  static auto makeSourceToRootReportData(const Clock& clock, std::size_t clock_index, const Net& source_net,
                                         const ClockSynthesis::SourceToRootBuildResult& result, ClockTreeSynthesisPhase synthesis_phase)
      -> ClockTreeReportData;
  static auto merge(ClockTreeReportData& target, const ClockTreeReportData& source) -> void;
};

}  // namespace icts
