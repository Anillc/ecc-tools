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
 * @file STAAdapterTimingUpdate.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter timing update and state reset implementation.
 */

#include <glog/logging.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "StrMap.hh"
#include "TimingDBAdapter.hh"
#include "Type.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "liberty/Lib.hh"
#include "sdc/SdcClock.hh"
#include "sdc/SdcConstrain.hh"
#include "sta/Sta.hh"
#include "sta/StaClock.hh"
#include "sta/StaData.hh"
#include "sta/StaPathData.hh"
#include "sta/StaVertex.hh"

namespace icts {
namespace {

constexpr std::size_t kWorstSkewAverageLimit = 10U;

struct AnalysisModeDescriptor
{
  const char* name = "";
  ista::AnalysisMode mode = ista::AnalysisMode::kMax;
  bool larger_skew_is_worse = true;
};

auto collectSeqPathData(ista::StaSeqPathGroup* seq_path_group, ista::AnalysisMode mode) -> std::vector<ista::StaSeqPathData*>
{
  std::vector<ista::StaSeqPathData*> path_datas;
  if (seq_path_group == nullptr) {
    return path_datas;
  }

  ista::StaPathEnd* path_end = nullptr;
  ista::StaPathData* path_data = nullptr;
  FOREACH_PATH_GROUP_END(seq_path_group, path_end)
  FOREACH_PATH_END_DATA(path_end, mode, path_data)
  {
    auto* seq_path_data = dynamic_cast<ista::StaSeqPathData*>(path_data);
    if (seq_path_data != nullptr) {
      path_datas.push_back(seq_path_data);
    }
  }
  return path_datas;
}

auto hasSeqPathData(ista::StaSeqPathGroup* seq_path_group, ista::AnalysisMode mode) -> bool
{
  if (seq_path_group == nullptr) {
    return false;
  }

  ista::StaPathEnd* path_end = nullptr;
  ista::StaPathData* path_data = nullptr;
  FOREACH_PATH_GROUP_END(seq_path_group, path_end)
  FOREACH_PATH_END_DATA(path_end, mode, path_data)
  {
    if (dynamic_cast<ista::StaSeqPathData*>(path_data) != nullptr) {
      return true;
    }
  }
  return false;
}

auto sortBySkewSeverity(std::vector<ista::StaSeqPathData*>& path_datas, bool larger_skew_is_worse) -> void
{
  std::ranges::sort(path_datas, [larger_skew_is_worse](ista::StaSeqPathData* lhs, ista::StaSeqPathData* rhs) -> bool {
    if (lhs == nullptr || rhs == nullptr) {
      return rhs != nullptr;
    }
    return larger_skew_is_worse ? lhs->getSkew() > rhs->getSkew() : lhs->getSkew() < rhs->getSkew();
  });
}

auto toNs(int64_t fs_value) -> double
{
  return static_cast<double>(fs_value) / static_cast<double>(g_ns2fs);
}

auto vertexName(ista::StaClockData* clock_data) -> std::string
{
  if (clock_data == nullptr || clock_data->get_own_vertex() == nullptr) {
    return "n/a";
  }
  return clock_data->get_own_vertex()->getNameWithCellName();
}

auto buildLatencySkewMetrics(const std::string& clock_name, const AnalysisModeDescriptor& descriptor,
                             std::vector<ista::StaSeqPathData*> path_datas) -> std::optional<STAAdapter::ClockLatencySkewMetrics>
{
  if (path_datas.empty()) {
    return std::nullopt;
  }

  sortBySkewSeverity(path_datas, descriptor.larger_skew_is_worse);
  auto* worst_path = path_datas.front();
  if (worst_path == nullptr || worst_path->get_launch_clock_data() == nullptr || worst_path->get_capture_clock_data() == nullptr) {
    return std::nullopt;
  }

  const std::size_t sample_count = std::min(kWorstSkewAverageLimit, path_datas.size());
  double skew_sum_ns = 0.0;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    auto* path_data = path_datas.at(index);
    if (path_data != nullptr) {
      skew_sum_ns += toNs(path_data->getSkew());
    }
  }

  return STAAdapter::ClockLatencySkewMetrics{
      .clock_name = clock_name,
      .analysis_mode = descriptor.name,
      .launch_pin = vertexName(worst_path->get_launch_clock_data()),
      .capture_pin = vertexName(worst_path->get_capture_clock_data()),
      .launch_latency_ns = toNs(worst_path->get_launch_clock_data()->get_arrive_time()),
      .capture_latency_ns = toNs(worst_path->get_capture_clock_data()->get_arrive_time()),
      .worst_skew_ns = toNs(worst_path->getSkew()),
      .average_worst_skew_ns = sample_count == 0U ? 0.0 : skew_sum_ns / static_cast<double>(sample_count),
      .path_count = path_datas.size(),
      .average_sample_count = sample_count,
  };
}

auto hasClockPathData(ista::Sta* ista, const std::string& clock_name) -> bool
{
  if (ista == nullptr) {
    return false;
  }

  for (const auto& [clock, seq_path_group] : ista->get_clock_groups()) {
    if (clock == nullptr || clock_name != clock->get_clock_name()) {
      continue;
    }
    return hasSeqPathData(seq_path_group.get(), ista::AnalysisMode::kMax) || hasSeqPathData(seq_path_group.get(), ista::AnalysisMode::kMin);
  }
  return false;
}

auto buildClockTimingMetrics(ista::TimingEngine* timing_engine, ista::StaClock* sta_clock) -> STAAdapter::ClockTimingMetrics
{
  const auto* clock_name = sta_clock->get_clock_name();
  STAAdapter::ClockTimingMetrics metrics;
  metrics.setup_tns = timing_engine->getTNS(clock_name, ista::AnalysisMode::kMax);
  metrics.setup_wns = timing_engine->getWNS(clock_name, ista::AnalysisMode::kMax);
  metrics.hold_tns = timing_engine->getTNS(clock_name, ista::AnalysisMode::kMin);
  metrics.hold_wns = timing_engine->getWNS(clock_name, ista::AnalysisMode::kMin);

  const double suggested_period_ns = sta_clock->getPeriodNs() - metrics.setup_wns;
  metrics.suggest_freq = suggested_period_ns > 0.0 ? 1000.0 / suggested_period_ns : 0.0;
  return metrics;
}

}  // namespace

auto STAAdapter::updateTiming() -> void
{
  LOG_FATAL_IF(!sta_adapter_internal::HasFullDesignTimingContext()) << "STA full-design context is not ready before timing update; call "
                                                                       "STAAdapter::refreshFullDesignTimingContext() after iDB projection.";
  sta_adapter_internal::GetStaEngine()->updateTiming();
}

auto STAAdapter::reportTiming() -> bool
{
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    LOG_WARNING << "STA timing report skipped: full-design context is not ready.";
    return false;
  }

  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  timing_engine->reportTiming({}, true, true);
  return true;
}

auto STAAdapter::refreshFullDesignTimingContext() -> void
{
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  LOG_FATAL_IF(timing_engine->get_db_adapter() == nullptr) << "STA full-design refresh requires STAAdapter::init() first.";

  timing_engine->set_num_threads(sta_adapter_internal::kStaThreadCount);
  sta_adapter_internal::ConfigureStaWorkspace(timing_engine, "sta");
  getInst().resetStaTransientState();
  timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  sta_adapter_internal::LoadConfiguredSdcIfPresent(timing_engine);
  timing_engine->buildGraph();
  timing_engine->initRcTree();
  timing_engine->get_ista()->set_n_worst_path_per_clock(sta_adapter_internal::kWorstPathPerClock);
}

auto STAAdapter::setPropagatedClocks() -> std::size_t
{
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    LOG_WARNING << "Propagated-clock setup skipped: STA full-design context is not ready.";
    return 0U;
  }

  auto* ista = sta_adapter_internal::GetStaEngine()->get_ista();
  if (ista == nullptr) {
    LOG_WARNING << "Propagated-clock setup skipped: iSTA context is null.";
    return 0U;
  }

  auto& constrain = ista->get_constrains();
  if (constrain == nullptr) {
    LOG_WARNING << "Propagated-clock setup skipped: iSTA SDC constrain is null.";
    return 0U;
  }

  auto& sdc_clocks = constrain->get_sdc_clocks();
  std::size_t propagated_count = 0U;
  for (const auto& [_, sdc_clock] : sdc_clocks) {
    if (sdc_clock == nullptr) {
      continue;
    }
    sdc_clock->set_is_propagated();
    ++propagated_count;
  }
  return propagated_count;
}

auto STAAdapter::queryClockTiming(const std::string& clock_name) -> std::optional<ClockTimingMetrics>
{
  if (clock_name.empty()) {
    LOG_WARNING << "Clock timing query skipped: clock name is empty.";
    return std::nullopt;
  }
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    LOG_WARNING << "Clock timing query skipped for \"" << clock_name << "\": STA full-design context is not ready.";
    return std::nullopt;
  }

  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  ista::StaClock* sta_clock = nullptr;
  for (auto* clock : timing_engine->getClockList()) {
    if (clock != nullptr && clock_name == clock->get_clock_name()) {
      sta_clock = clock;
      break;
    }
  }
  if (sta_clock == nullptr) {
    LOG_WARNING << "Clock timing query skipped: clock \"" << clock_name << "\" is not found in STA.";
    return std::nullopt;
  }
  if (!hasClockPathData(timing_engine->get_ista(), clock_name)) {
    LOG_WARNING << "Clock timing query skipped: clock \"" << clock_name << "\" has no STA path data.";
    return std::nullopt;
  }

  return buildClockTimingMetrics(timing_engine, sta_clock);
}

auto STAAdapter::queryClockTimings() -> std::vector<ClockTimingRecord>
{
  std::vector<ClockTimingRecord> records;
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    LOG_WARNING << "Clock timing query skipped: STA full-design context is not ready.";
    return records;
  }

  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  auto* ista = timing_engine->get_ista();
  for (auto* sta_clock : timing_engine->getClockList()) {
    if (sta_clock == nullptr) {
      continue;
    }
    const std::string clock_name = sta_clock->get_clock_name();
    if (!hasClockPathData(ista, clock_name)) {
      LOG_WARNING << "Clock timing query skipped: clock \"" << clock_name << "\" has no STA path data.";
      continue;
    }
    records.push_back(ClockTimingRecord{
        .clock_name = clock_name,
        .metrics = buildClockTimingMetrics(timing_engine, sta_clock),
    });
  }
  return records;
}

auto STAAdapter::queryClockLatencySkew() -> std::vector<ClockLatencySkewMetrics>
{
  std::vector<ClockLatencySkewMetrics> metrics;
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    LOG_WARNING << "Clock latency/skew query skipped: STA full-design context is not ready.";
    return metrics;
  }

  auto* ista = sta_adapter_internal::GetStaEngine()->get_ista();
  if (ista == nullptr) {
    LOG_WARNING << "Clock latency/skew query skipped: iSTA context is null.";
    return metrics;
  }

  const std::vector<AnalysisModeDescriptor> descriptors = {
      {.name = "setup", .mode = ista::AnalysisMode::kMax, .larger_skew_is_worse = true},
      {.name = "hold", .mode = ista::AnalysisMode::kMin, .larger_skew_is_worse = false},
  };
  for (const auto& [clock, seq_path_group] : ista->get_clock_groups()) {
    const std::string clock_name = clock != nullptr ? clock->get_clock_name() : "";
    if (clock_name.empty()) {
      continue;
    }
    for (const auto& descriptor : descriptors) {
      auto path_datas = collectSeqPathData(seq_path_group.get(), descriptor.mode);
      auto mode_metrics = buildLatencySkewMetrics(clock_name, descriptor, std::move(path_datas));
      if (mode_metrics.has_value()) {
        metrics.push_back(*mode_metrics);
      } else {
        LOG_WARNING << "Clock latency/skew query found no STA path data for clock \"" << clock_name << "\" in " << descriptor.name
                    << " mode.";
      }
    }
  }
  return metrics;
}

auto STAAdapter::queryCharInputPinCap(const std::string& cell_master) -> double
{
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("input pin cap", cell_master)
                << " failed: liberty cell not found; return 0.0.";
    return 0.0;
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    return 0.0;
  }
  return sta_adapter_internal::ConvertLibCapToPf(lib_cell, input->get_port_cap());
}

auto STAAdapter::queryPinCapacitance(const Pin* pin) -> double
{
  if (pin == nullptr) {
    LOG_WARNING << "Null pin provided when querying pin capacitance.";
    return 0.0;
  }

  auto* inst = pin->get_inst();
  const std::string pin_full_name = inst != nullptr ? (inst->get_name() + "/" + pin->get_name()) : pin->get_name();
  if (inst == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: CTS pin has no owning instance for " << pin_full_name << ".";
    return 0.0;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    LOG_WARNING << "Pin-cap query skipped: CTS instance has no cell master for " << pin_full_name << ".";
    return 0.0;
  }

  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty cell \"" << cell_master << "\" is not found for " << pin_full_name << ".";
    return 0.0;
  }

  const auto port_name = sta_adapter_internal::NormalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty port \"" << port_name << "\" is not found on cell " << cell_master << ".";
    return 0.0;
  }
  if (lib_port->isInput() == 0U) {
    return 0.0;
  }

  return sta_adapter_internal::QueryLibPortCapacitancePf(lib_cell, lib_port);
}

auto STAAdapter::queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>
{
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("buffer ports", cell_master)
                << " failed: liberty cell not found; return empty port names.";
    return {"", ""};
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  const std::string in_name = input != nullptr ? input->get_port_name() : "";
  const std::string out_name = output != nullptr ? output->get_port_name() : "";
  return {in_name, out_name};
}

auto STAAdapter::resetStaTransientState() -> void
{
  ipower::Power::destroyPower();

  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  auto* ista = timing_engine->get_ista();
  if (ista != nullptr) {
    ista->resetAllRcNet();
    ista->resetSdcConstrain();
  }
  timing_engine->resetNetlist();
  timing_engine->resetGraph();
}

}  // namespace icts
