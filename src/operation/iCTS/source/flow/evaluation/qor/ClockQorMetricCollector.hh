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
 * @file ClockQorMetricCollector.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Clock-net QoR metric roles, measurements, and collector contracts.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "design/ClockDAG.hh"
#include "evaluation/qor/QorEvaluation.hh"
#include "logger/SchemaForward.hh"

namespace icts {

class Clock;
class ClockLayout;
class Design;
class Inst;
class Net;
class STAAdapter;
class Wrapper;

namespace qor_evaluation {

enum class ClockNetRole
{
  kSourceToRoot,
  kTrunk,
  kLeaf
};

struct ClockNetMeasurement
{
  ClockNetRole role = ClockNetRole::kTrunk;
  int64_t wirelength_dbu = 0;
  int64_t hpwl_dbu = 0;
};

struct ClockNetMeasurementInput
{
  const Config* config = nullptr;
  STAAdapter* sta_adapter = nullptr;
  Wrapper* wrapper = nullptr;
  Net* net = nullptr;
  ClockNetRole role = ClockNetRole::kTrunk;
  bool install_sta_rc_tree = false;
};

struct ClockTimingAppendInput
{
  STAAdapter* sta_adapter = nullptr;
  SchemaWriter* reporter = nullptr;
  bool query_sta_timing = false;
  QorSummary* summary = nullptr;
};

struct RootInputToLeafOutputProbeReportInput
{
  STAAdapter* sta_adapter = nullptr;
  Design* design = nullptr;
  SchemaWriter* reporter = nullptr;
  const std::vector<Clock*>* clocks = nullptr;
  const ClockLayout* clock_layout = nullptr;
  bool query_sta_timing = false;
};

auto ClearStatistics(Qor& statistics) -> void;
auto ClearSummary(QorSummary& summary) -> void;
auto SyncCompatibilityAliases(QorSummary& summary) -> void;
auto AppendPathDepthStats(const ClockDAG::PathBufferStats& path_stats, QorSummary& summary) -> void;
auto ClassifyClockNet(const Clock& clock, const Net* net) -> ClockNetRole;
auto AccumulateInstStatistics(STAAdapter& sta_adapter, const Inst& inst, Qor& statistics) -> void;
auto InstallClockNetRcTreeAndMeasure(const ClockNetMeasurementInput& input) -> std::optional<ClockNetMeasurement>;
auto AppendClockNetStatistics(const std::vector<ClockNetMeasurement>& measurements, QorSummary& summary, Qor& statistics) -> void;
auto AppendClockTimings(const ClockTimingAppendInput& input) -> void;
auto AppendClockLatencySkew(STAAdapter& sta_adapter, QorSummary& summary) -> void;
auto EmitEvaluationSummary(SchemaWriter& reporter, const QorSummary& summary, bool refreshed_sta) -> void;
auto EmitRootInputToLeafOutputProbeReport(const RootInputToLeafOutputProbeReportInput& input) -> void;

}  // namespace qor_evaluation
}  // namespace icts
