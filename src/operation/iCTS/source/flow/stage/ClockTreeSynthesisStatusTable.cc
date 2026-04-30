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
 * @file ClockTreeSynthesisStatusTable.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Typed CTS clock-tree synthesis status row assembly implementation.
 */

#include "stage/ClockTreeSynthesisStatusTable.hh"

#include <string>

#include "design/Clock.hh"

namespace icts {
namespace {

auto sinkDomainLabel(CTSSinkDomain sink_domain) -> const char*
{
  if (sink_domain == CTSSinkDomain::kUnknown) {
    return "none";
  }
  return ToString(sink_domain);
}

}  // namespace

auto ToString(ClockTreeSynthesisStatus status) -> const char*
{
  switch (status) {
    case ClockTreeSynthesisStatus::kSkipped:
      return "skipped";
    case ClockTreeSynthesisStatus::kFailed:
      return "failed";
    case ClockTreeSynthesisStatus::kFinished:
      return "finished";
  }
  return "failed";
}

auto ClockTreeSynthesisStatusTable::append(const Clock& clock, ClockTreeSynthesisStatus status, CTSSinkDomain sink_domain,
                                           std::size_t valid_sinks, std::size_t sink_domain_sinks, const std::string& detail) -> void
{
  _rows->push_back({
      clock.get_clock_name(),
      clock.get_clock_net_name(),
      ToString(status),
      sinkDomainLabel(sink_domain),
      std::to_string(valid_sinks),
      std::to_string(sink_domain_sinks),
      detail,
  });
}

auto ClockTreeSynthesisStatusTable::appendNoDomain(const Clock& clock, ClockTreeSynthesisStatus status, std::size_t valid_sinks,
                                                   std::size_t sink_domain_sinks, const std::string& detail) -> void
{
  append(clock, status, CTSSinkDomain::kUnknown, valid_sinks, sink_domain_sinks, detail);
}

auto ClockTreeSynthesisStatusTable::appendNullClock(ClockTreeSynthesisStatus status, const std::string& detail) -> void
{
  _rows->push_back({"", "", ToString(status), "none", "0", "0", detail});
}

}  // namespace icts
