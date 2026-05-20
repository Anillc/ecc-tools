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
 * @file Optimization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS post-synthesis optimization flow facade implementation.
 */

#include "optimization/Optimization.hh"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "FastSta.hh"
#include "Log.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "optimization/clock_sizing_edit/ClockSizingAcceptedEdit.hh"
#include "optimization/model/ClockSizingOptimizationData.hh"
#include "optimization/options/OptimizationOptions.hh"
#include "optimization/preparation/OptimizationPreparation.hh"
#include "optimization/report/OptimizationReport.hh"
#include "optimization/solver/OptimizationSolver.hh"

namespace icts {
namespace oi = clock_sizing_optimization;

auto Optimization::run(ClockLayout& clock_layout, CharacterizationLibrary& characterization_library) -> OptimizationResult
{
  (void) characterization_library;
  OptimizationResult result;
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("optimization");
  auto stage = SCHEMA_WRITER_INST.beginStage("Optimization", "Optimize synthesized CTS buffers with CTS fast STA", {},
                                             schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Optimization Overview");

  const auto& options = oi::DefaultOptimizationOptions();
  if (!oi::ValidateOptimizationOptions(options)) {
    LOG_ERROR << "Optimization: internal optimizer options are invalid.";
    result.success = false;
    (void) runtime.failed();
    stage.failed({{"reason", "invalid_optimizer_options"}});
    return result;
  }

  const auto start_time = std::chrono::steady_clock::now();
  const auto clocks = DESIGN_INST.get_clocks();
  result.clock_count = clocks.size();
  const auto master_infos = oi::CollectClockSizingBufferMasters();
  if (master_infos.empty()) {
    LOG_WARNING << "Optimization: skip because no legal buffer sizing candidates are available.";
    (void) runtime.finish("skipped");
    stage.skip({{"reason", "no_sizing_candidates"}});
    return result;
  }

  auto stage_start = std::chrono::steady_clock::now();
  const auto route_tree_by_net = oi::BuildClockSizingRouteTrees(clocks);
  const double route_tree_cache_runtime_s = oi::ElapsedSeconds(stage_start);
  const double target_skew_ns = std::max(0.0, CONFIG_INST.get_skew_bound());
  schema::EmitKeyValueTable("CTS Optimization Setup", {{"timing_source", "cts_fast_sta_incremental"},
                                                       {"target_skew", oi::FormatNs(target_skew_ns)},
                                                       {"candidate_master_count", std::to_string(master_infos.size())}});
  schema::EmitKeyValueTable("CTS Optimization Global Profile", {{"build_route_tree_cache", oi::FormatSeconds(route_tree_cache_runtime_s)},
                                                                {"cached_route_tree_count", std::to_string(route_tree_by_net.size())}});

  std::string no_op_reason = "no_optimizable_clock";
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    const auto clock_start = std::chrono::steady_clock::now();
    oi::ClockSizingRuntimeProfile outer_profile;
    outer_profile.build_route_tree_cache_s = route_tree_cache_runtime_s;
    stage_start = std::chrono::steady_clock::now();
    const auto route_geometry = oi::BuildClockRouteGeometry(clock_layout, clock_index);
    const auto clock_id = FastSTA::buildClockContext(*clock, route_geometry);
    outer_profile.build_fast_sta_context_s = oi::ElapsedSeconds(stage_start);

    auto graph_profile = oi::CaptureGraphProfile(clock_id);
    graph_profile.build_route_tree_cache_s = outer_profile.build_route_tree_cache_s;
    graph_profile.build_fast_sta_context_s = outer_profile.build_fast_sta_context_s;
    outer_profile = graph_profile;

    stage_start = std::chrono::steady_clock::now();
    if (!oi::InjectRouteTrees(clock_id, *clock, route_tree_by_net)) {
      outer_profile.inject_route_trees_s = oi::ElapsedSeconds(stage_start);
      oi::EmitClockProfile(*clock, outer_profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA context build failed.";
      (void) FastSTA::eraseClockContext(clock_id);
      no_op_reason = "fast_sta_context_failed";
      continue;
    }
    outer_profile.inject_route_trees_s = oi::ElapsedSeconds(stage_start);

    stage_start = std::chrono::steady_clock::now();
    auto buffers = oi::CollectClockSizingBuffers(clock_id, master_infos);
    outer_profile.collect_optimizable_buffers_s = oi::ElapsedSeconds(stage_start);
    outer_profile.optimizable_buffer_count = buffers.size();
    if (buffers.empty()) {
      oi::EmitClockProfile(*clock, outer_profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because no resizable buffers are available.";
      (void) FastSTA::eraseClockContext(clock_id);
      no_op_reason = "no_resizable_buffers";
      continue;
    }

    stage_start = std::chrono::steady_clock::now();
    const auto cap_baseline = oi::CollectClockSizingCapLimits(clock_id);
    outer_profile.collect_cap_baseline_s = oi::ElapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const auto slew_baseline = oi::CollectClockSizingSlewLimits(clock_id);
    outer_profile.collect_slew_baseline_s = oi::ElapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const bool use_scalable_solver = oi::ShouldUseScalableSolver(clock_id, buffers);
    LOG_INFO << "Optimization: clock \"" << clock->get_clock_name() << "\" uses "
             << (use_scalable_solver ? "scalable timing-only batch solver" : "exact full-power batch solver") << ".";
    auto summary = use_scalable_solver ? oi::SolveClockScalable(clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns)
                                       : oi::SolveClock(clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns);
    outer_profile.solve_clock_s = oi::ElapsedSeconds(stage_start);
    oi::CopyOuterProfile(summary.profile, outer_profile);
    const auto clock_end = std::chrono::steady_clock::now();
    const double clock_runtime_s = std::chrono::duration<double>(clock_end - clock_start).count();
    if (!summary.valid) {
      oi::EmitClockProfile(*clock, summary.profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA solver failed with reason "
                  << summary.stop_reason << ".";
      (void) FastSTA::eraseClockContext(clock_id);
      no_op_reason = summary.stop_reason.empty() ? "solver_failed" : summary.stop_reason;
      continue;
    }
    stage_start = std::chrono::steady_clock::now();
    if (!summary.accepted_edits.empty() && !oi::ApplyClockSizingAcceptedEdits(summary.accepted_edits, buffers, clock_layout)) {
      result.success = false;
      (void) runtime.failed();
      stage.failed({{"reason", "accepted_edit_apply_failed"}});
      return result;
    }
    summary.profile.apply_accepted_edits_s = oi::ElapsedSeconds(stage_start);
    oi::EmitClockSummary(*clock, summary, target_skew_ns, clock_runtime_s);
    oi::EmitClockProfile(*clock, summary.profile);
    if (summary.accepted_edits.empty() && !summary.stop_reason.empty()
        && (no_op_reason == "no_optimizable_clock" || no_op_reason == "target_met")) {
      no_op_reason = summary.stop_reason;
    }
    result.optimized = result.optimized || !summary.accepted_edits.empty();
    result.optimized_clock_count += summary.accepted_edits.empty() ? 0U : 1U;
    result.accepted_edit_count += summary.accepted_edit_count;
    (void) FastSTA::eraseClockContext(clock_id);
  }

  const auto end_time = std::chrono::steady_clock::now();
  const double total_runtime_s = std::chrono::duration<double>(end_time - start_time).count();
  schema::EmitKeyValueTable("CTS Optimization Summary", {
                                                            {"runtime", logformat::FormatWithUnit(total_runtime_s, "s")},
                                                            {"clock_count", std::to_string(result.clock_count)},
                                                            {"optimized_clock_count", std::to_string(result.optimized_clock_count)},
                                                            {"accepted_edit_count", std::to_string(result.accepted_edit_count)},
                                                            {"status", result.optimized ? "optimized" : "no_op"},
                                                        });
  LOG_INFO << "CTS optimization finished with " << result.accepted_edit_count << " accepted sizing edits across "
           << result.optimized_clock_count << " clocks.";
  if (result.optimized) {
    (void) runtime.finished();
    stage.finished({{"accepted_edit_count", std::to_string(result.accepted_edit_count)}});
  } else {
    (void) runtime.finish("no_op");
    stage.skip({{"reason", no_op_reason}});
  }
  return result;
}

}  // namespace icts
