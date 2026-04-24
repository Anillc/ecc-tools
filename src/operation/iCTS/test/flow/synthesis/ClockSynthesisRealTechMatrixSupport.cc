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
 * @file ClockSynthesisRealTechMatrixSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief ARM9 real-tech matrix support for ClockSynthesis smoke tests.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ClockSynthesisRealTechSmokeSupport.hh"
#include "HTreeTopologyChar.hh"
#include "PatternId.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/config/Config.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test::synthesis_realtech_smoke {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

auto MakeSkipResult(const std::string& reason) -> ClockSynthesisMatrixRunResult
{
  ClockSynthesisMatrixRunResult result;
  result.skipped = true;
  result.skip_reason = reason;
  return result;
}

auto MakeCasePrefix(unsigned wire_length_iterations, unsigned slew_cap_steps) -> std::string
{
  return "iter=" + std::to_string(wire_length_iterations) + ", step=" + std::to_string(slew_cap_steps) + ": ";
}

auto AppendCaseFailures(unsigned wire_length_iterations, unsigned slew_cap_steps, const icts::ClockSynthesis::BuildResult& result,
                        double runtime_s, const ClockSynthesisExperimentRecord& record, std::vector<std::string>& failure_messages) -> void
{
  const std::string prefix = MakeCasePrefix(wire_length_iterations, slew_cap_steps);
  if (!result.success) {
    failure_messages.push_back(prefix + "failure_reason=" + result.htree_result.failure_reason);
  }
  if (result.sink_clustering_enabled) {
    failure_messages.push_back(prefix + "sink clustering should be disabled");
  }
  if (!result.cluster_buffers.empty()) {
    failure_messages.push_back(prefix + "cluster buffers should be empty");
  }
  if (runtime_s > kArm9SynthesisRuntimeBudgetS) {
    failure_messages.push_back(prefix + "runtime_s=" + std::to_string(runtime_s) + " exceeds budget");
  }
  if (record.final_frontier_count == 0U) {
    failure_messages.push_back(prefix + "final frontier count is zero");
  }
  if (!result.htree_result.best_char.has_value()) {
    failure_messages.push_back(prefix + "best htree char is missing");
  }
  if (record.char_wire_length_unit_um <= 0.0) {
    failure_messages.push_back(prefix + "char wire length unit is not positive");
  }
  if (record.char_wire_length_iterations > wire_length_iterations) {
    failure_messages.push_back(prefix + "char wire length iterations exceed requested iteration count");
  }
}

}  // namespace

auto EvaluateArm9FullSinkNonClusteredExperimentMatrix() -> ClockSynthesisMatrixRunResult
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    return MakeSkipResult(setup_state.summary);
  }

  const auto selected_clock = SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), 2U);
  if (!selected_clock.has_value()) {
    return MakeSkipResult("No DEF-derived clock net exposes source plus at least two sinks.");
  }
  const auto& selected_clock_data = selected_clock.value();

  ClockSynthesisMatrixRunResult matrix_result;
  matrix_result.selection = selected_clock_data;
  matrix_result.records.reserve(kArm9ExperimentIterations.size() * kArm9ExperimentSteps.size());

  for (const unsigned wire_length_iterations : kArm9ExperimentIterations) {
    for (const unsigned slew_cap_steps : kArm9ExperimentSteps) {
      std::ostringstream scenario_name_stream;
      scenario_name_stream << "clock_synthesis_arm9_full_sink_iter" << wire_length_iterations << "_step" << slew_cap_steps;
      const std::string scenario_name = scenario_name_stream.str();

      realtech_support::RealTechCharSession char_session;
      if (const auto prepare_error
          = char_session.prepare(scenario_name, std::nullopt, kSynthesisSmokeMaxSlewNs, kSynthesisSmokeMaxCapPf, true);
          prepare_error.has_value()) {
        return MakeSkipResult(*prepare_error);
      }

      CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
      CONFIG_INST.set_slew_steps(slew_cap_steps);
      CONFIG_INST.set_cap_steps(slew_cap_steps);

      icts::ClockSynthesis::BuildOptions options;
      SetEnableSinkClustering(options, false);

      const auto runtime_start = std::chrono::steady_clock::now();
      const auto result = icts::ClockSynthesis::build(selected_clock_data.source, selected_clock_data.sinks, options);
      const auto runtime_end = std::chrono::steady_clock::now();
      const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();

      ClockSynthesisExperimentRecord record{
          .wire_length_iterations = wire_length_iterations,
          .slew_cap_steps = slew_cap_steps,
          .runtime_s = runtime_s,
          .success = result.success,
          .sink_count = selected_clock_data.sinks.size(),
          .char_wire_length_unit_um = result.htree_result.char_wire_length_unit_um,
          .char_wire_length_iterations = result.htree_result.char_wire_length_iterations,
          .char_grid_adapted = result.htree_result.char_grid_adapted,
          .used_boundary_fallback = result.htree_result.used_boundary_fallback,
          .failure_reason = result.htree_result.failure_reason,
      };

      const auto* selected_summary = FindSelectedDepthSummary(result.htree_result);
      if (selected_summary != nullptr) {
        record.final_frontier_count = selected_summary->final_frontier_count;
      }
      if (result.htree_result.selected_depth.has_value()) {
        record.selected_depth = *result.htree_result.selected_depth;
      }
      if (result.htree_result.best_char.has_value()) {
        record.best_pattern_id = result.htree_result.best_char->get_pattern_id().local_id;
        record.best_delay_ns = result.htree_result.best_char->get_delay();
        record.best_power_w = result.htree_result.best_char->get_power();
      }
      matrix_result.records.push_back(record);
      AppendCaseFailures(wire_length_iterations, slew_cap_steps, result, runtime_s, record, matrix_result.failure_messages);
    }
  }

  matrix_result.report_written = WriteClockSynthesisMatrixReport(
      kArm9ClockSynthesisScenario, "matrix_report.txt",
      FormatClockSynthesisExperimentReport(kArm9ClockSynthesisScenario, selected_clock_data, true, matrix_result.records));
  return matrix_result;
}

}  // namespace icts_test::synthesis_realtech_smoke
