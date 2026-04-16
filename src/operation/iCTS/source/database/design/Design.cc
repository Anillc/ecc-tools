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
 * @file Design.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-16
 * @brief Design-owned reporting helpers for iCTS clocks
 */

#include "database/design/Design.hh"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "database/design/Clock.hh"
#include "database/design/Inst.hh"
#include "database/design/Pin.hh"
#include "utils/logger/Schema.hh"

namespace icts {
namespace {

struct ClockDistributionStats
{
  std::size_t nets = 0U;
  std::size_t total_sinks = 0U;
  std::size_t flipflop_sinks = 0U;
  std::size_t buffer_sinks = 0U;
  std::size_t no_inst_sinks = 0U;
};

auto summarizeClockGroup(const std::vector<Clock*>& clocks) -> ClockDistributionStats
{
  ClockDistributionStats stats;
  stats.nets = clocks.size();

  for (const auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    for (const auto* pin : clock->get_loads()) {
      ++stats.total_sinks;

      const auto* inst = pin->get_inst();
      if (inst == nullptr) {
        ++stats.no_inst_sinks;
      } else if (inst->is_flipflop()) {
        ++stats.flipflop_sinks;
      } else {
        ++stats.buffer_sinks;
      }
    }
  }

  return stats;
}

}  // namespace

Design::Design() = default;

Design::~Design() = default;

auto Design::reset() -> void
{
  _clocks.clear();
}

auto Design::get_clocks() const -> std::vector<Clock*>
{
  std::vector<Clock*> clocks;
  clocks.reserve(_clocks.size());
  for (const auto& clock : _clocks) {
    clocks.push_back(clock.get());
  }
  return clocks;
}

auto Design::set_clocks(std::vector<std::unique_ptr<Clock>> clocks) -> void
{
  _clocks = std::move(clocks);
}

auto Design::add_clock(std::unique_ptr<Clock> clock) -> Clock*
{
  if (clock == nullptr) {
    return nullptr;
  }

  auto* raw = clock.get();
  _clocks.push_back(std::move(clock));
  return raw;
}

auto Design::emitClockDistributionSummary(const std::string& title) const -> void
{
  std::map<std::string, std::vector<Clock*>> clock_groups;
  for (auto* clock : get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    clock_groups[clock->get_clock_name()].push_back(clock);
  }

  if (clock_groups.empty()) {
    schema::EmitTable(title, {"status"}, {{"No clocks available for distribution summary."}});
    return;
  }

  schema::TableRows rows;
  rows.reserve(clock_groups.size() + 1U);

  ClockDistributionStats total_stats;
  for (const auto& [clock_name, clocks] : clock_groups) {
    const auto stats = summarizeClockGroup(clocks);
    total_stats.nets += stats.nets;
    total_stats.total_sinks += stats.total_sinks;
    total_stats.flipflop_sinks += stats.flipflop_sinks;
    total_stats.buffer_sinks += stats.buffer_sinks;
    total_stats.no_inst_sinks += stats.no_inst_sinks;

    rows.push_back({
        clock_name,
        std::to_string(stats.nets),
        std::to_string(stats.total_sinks),
        std::to_string(stats.flipflop_sinks),
        std::to_string(stats.buffer_sinks),
        std::to_string(stats.no_inst_sinks),
    });
  }

  rows.push_back({
      "TOTAL",
      std::to_string(total_stats.nets),
      std::to_string(total_stats.total_sinks),
      std::to_string(total_stats.flipflop_sinks),
      std::to_string(total_stats.buffer_sinks),
      std::to_string(total_stats.no_inst_sinks),
  });

  schema::EmitTable(title, {"Clock", "Nets", "Total Sinks", "FlipFlop Sinks", "Buffer Sinks", "No-Inst Sinks"}, rows);
}

}  // namespace icts
