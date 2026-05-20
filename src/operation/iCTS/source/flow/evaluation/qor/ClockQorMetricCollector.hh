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

namespace icts {

class Clock;
class ClockLayout;
class Inst;
class Net;

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

auto ClearStatistics(Qor& statistics) -> void;
auto ClearSummary(QorSummary& summary) -> void;
auto SyncCompatibilityAliases(QorSummary& summary) -> void;
auto AppendPathDepthStats(const ClockDAG::PathBufferStats& path_stats, QorSummary& summary) -> void;
auto ClassifyClockNet(const Clock& clock, const Net* net) -> ClockNetRole;
auto AccumulateInstStatistics(const Inst& inst, Qor& statistics) -> void;
auto InstallClockNetRcTreeAndMeasure(Net* net, ClockNetRole role, bool install_sta_rc_tree) -> std::optional<ClockNetMeasurement>;
auto AppendClockNetStatistics(const std::vector<ClockNetMeasurement>& measurements, QorSummary& summary, Qor& statistics) -> void;
auto AppendClockTimings(bool query_sta_timing, QorSummary& summary) -> void;
auto AppendClockLatencySkew(QorSummary& summary) -> void;
auto EmitEvaluationSummary(const QorSummary& summary, bool refreshed_sta) -> void;
auto EmitRootInputToLeafOutputProbeReport(const std::vector<Clock*>& clocks, const ClockLayout* clock_layout, bool query_sta_timing)
    -> void;

}  // namespace qor_evaluation
}  // namespace icts
