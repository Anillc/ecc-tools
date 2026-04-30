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
 * @file ClockTreeSynthesisTransaction.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Owns CTS per-clock synthesis commit and rollback boundaries.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "synthesis/ClockSynthesis.hh"

namespace icts {

class CharacterizationLibrary;
class Clock;
class ClockTreeReportData;
class Pin;
struct ClockSinkDomainContext;
struct CTSClockTreeRunSummary;
class ClockTreeSynthesisStatusTable;

class ClockTreeSynthesisTransaction
{
 public:
  ClockTreeSynthesisTransaction(Clock& clock, std::size_t clock_index, ClockTreeReportData& report_data, CTSClockTreeRunSummary& summary,
                                ClockTreeSynthesisStatusTable& status_table, CharacterizationLibrary& characterization_library,
                                std::size_t valid_sinks);

  static auto rollbackClock(Clock& clock) -> void;
  static auto collectRootInputs(const std::vector<ClockSinkDomainContext>& sink_domains) -> std::vector<Pin*>;
  static auto collectSourceToRootLengthsUm(Pin* clock_source, const std::vector<Pin*>& root_inputs) -> std::vector<double>;

  auto synthesizeSinkDomain(const ClockSinkDomainContext& context, const std::vector<double>& source_to_root_lengths_um) -> bool;
  auto commitSinkDomain(const ClockSinkDomainContext& context, ClockSynthesis::BuildResult& synthesis_result, std::string& failure_reason)
      -> bool;
  auto synthesizeSourceToRoot(const std::vector<Pin*>& root_inputs) -> bool;

 private:
  Clock* _clock = nullptr;
  std::size_t _clock_index = 0U;
  ClockTreeReportData* _report_data = nullptr;
  CTSClockTreeRunSummary* _summary = nullptr;
  ClockTreeSynthesisStatusTable* _status_table = nullptr;
  CharacterizationLibrary* _characterization_library = nullptr;
  std::size_t _valid_sinks = 0U;
};

}  // namespace icts
