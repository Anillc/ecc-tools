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
 * @file HTreeBuilderRealTechMatrixSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief ARM9 real-tech matrix runner for HTreeBuilder tests.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "PatternId.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/config/Config.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/htree/HTreeBuilderRealTechSmokeSupport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

auto MakeSkipResult(const std::string& reason) -> Arm9ExperimentMatrixRunResult
{
  Arm9ExperimentMatrixRunResult result;
  result.skipped = true;
  result.skip_reason = reason;
  return result;
}

auto MakeCasePrefix(unsigned wire_length_iterations, unsigned slew_cap_steps) -> std::string
{
  return "iter=" + std::to_string(wire_length_iterations) + ", step=" + std::to_string(slew_cap_steps) + ": ";
}

auto ResolveArm9MatrixEnvName(bool omit_wire_length_unit) -> std::string_view
{
  return omit_wire_length_unit ? kRunArm9ExperimentAutoUnitEnv : kRunArm9ExperimentEnv;
}

auto ResolveArm9MatrixScenarioName(bool omit_wire_length_unit) -> std::string_view
{
  return omit_wire_length_unit ? kArm9ExperimentAutoUnitScenario : kArm9ExperimentScenario;
}

auto MakeArm9CaseScenarioName(bool omit_wire_length_unit, unsigned wire_length_iterations, unsigned slew_cap_steps) -> std::string
{
  std::ostringstream scenario_name_stream;
  scenario_name_stream << "htree_builder_arm9_full_sink";
  if (omit_wire_length_unit) {
    scenario_name_stream << "_auto_unit";
  }
  scenario_name_stream << "_iter" << wire_length_iterations << "_step" << slew_cap_steps;
  return scenario_name_stream.str();
}

auto MakeArm9ExperimentRecord(unsigned wire_length_iterations, unsigned slew_cap_steps, double runtime_s,
                              const icts::HTreeBuilder::BuildResult& result, std::size_t load_count) -> Arm9ExperimentRecord
{
  Arm9ExperimentRecord record{
      .wire_length_iterations = wire_length_iterations,
      .slew_cap_steps = slew_cap_steps,
      .runtime_s = runtime_s,
      .success = result.success,
      .load_count = load_count,
      .char_wire_length_unit_um = result.char_wire_length_unit_um,
      .char_wire_length_iterations = result.char_wire_length_iterations,
      .char_grid_adapted = result.char_grid_adapted,
      .used_boundary_fallback = result.used_boundary_fallback,
      .failure_reason = result.failure_reason,
  };

  if (const auto* selected_summary = FindSelectedDepthSummary(result); selected_summary != nullptr) {
    record.final_frontier_count = selected_summary->final_frontier_count;
  }
  if (result.selected_depth.has_value()) {
    record.selected_depth = *result.selected_depth;
  }
  if (result.best_char.has_value()) {
    record.best_pattern_id = result.best_char->get_pattern_id().local_id;
    record.best_delay_ns = result.best_char->get_delay();
    record.best_power_w = result.best_char->get_power();
  }
  return record;
}

auto AppendArm9CaseFailures(unsigned wire_length_iterations, unsigned slew_cap_steps, bool omit_wire_length_unit, double runtime_s,
                            const icts::HTreeBuilder::BuildResult& result, const Arm9ExperimentRecord& record,
                            std::vector<std::string>& failure_messages) -> void
{
  const std::string prefix = MakeCasePrefix(wire_length_iterations, slew_cap_steps);
  if (!result.success) {
    failure_messages.push_back(prefix + "failure_reason=" + result.failure_reason);
  }
  if (runtime_s > kArm9ExperimentRuntimeBudgetS) {
    failure_messages.push_back(prefix + "runtime_s=" + std::to_string(runtime_s) + " exceeds budget");
  }
  if (record.final_frontier_count == 0U) {
    failure_messages.push_back(prefix + "final frontier count is zero");
  }
  if (!result.best_char.has_value()) {
    failure_messages.push_back(prefix + "best htree char is missing");
  }
  if (omit_wire_length_unit && record.char_wire_length_unit_um <= 0.0) {
    failure_messages.push_back(prefix + "auto-derived char wire length unit is not positive");
  }
  if (omit_wire_length_unit && record.char_wire_length_iterations > wire_length_iterations) {
    failure_messages.push_back(prefix + "char wire length iterations exceed requested iteration count");
  }
}

auto ValidateSelectedClock(const RealClockLoadSelection& selected_clock, std::vector<std::string>& failure_messages) -> bool
{
  if (selected_clock.loads.size() < 2U) {
    failure_messages.emplace_back("Selected clock has fewer than two loads.");
    return false;
  }

  const std::size_t real_context_count = CountPinsWithRealContext(selected_clock.loads);
  if (real_context_count != selected_clock.loads.size()) {
    failure_messages.push_back("Selected clock loads do not carry complete DEF/CTS instance context: " + selected_clock.clock_name);
    return false;
  }
  return true;
}

}  // namespace

auto EvaluateArm9FullSinkExperimentMatrix(bool omit_wire_length_unit) -> Arm9ExperimentMatrixRunResult
{
  const std::string_view env_name = ResolveArm9MatrixEnvName(omit_wire_length_unit);
  if (!ReadEnvFlag(env_name)) {
    return MakeSkipResult("Set " + std::string(env_name) + "=1 to run the ARM9 full-sink H-tree experiment matrix.");
  }

  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    return MakeSkipResult(setup_state.summary);
  }

  const auto selected_clock = SelectLargestRealClockLoads(std::numeric_limits<std::size_t>::max());
  if (!selected_clock.has_value()) {
    return MakeSkipResult("No DEF-derived clock net exposes at least two CTS sink pins.");
  }

  Arm9ExperimentMatrixRunResult matrix_result;
  matrix_result.selection = *selected_clock;
  if (!ValidateSelectedClock(matrix_result.selection, matrix_result.failure_messages)) {
    return matrix_result;
  }

  matrix_result.records.reserve(kArm9ExperimentIterations.size() * kArm9ExperimentSteps.size());
  for (const unsigned wire_length_iterations : kArm9ExperimentIterations) {
    for (const unsigned slew_cap_steps : kArm9ExperimentSteps) {
      const std::string scenario_name = MakeArm9CaseScenarioName(omit_wire_length_unit, wire_length_iterations, slew_cap_steps);

      realtech_support::RealTechCharSession char_session;
      if (const auto prepare_error
          = char_session.prepare(scenario_name, std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, omit_wire_length_unit);
          prepare_error.has_value()) {
        return MakeSkipResult(*prepare_error);
      }

      CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
      CONFIG_INST.set_slew_steps(slew_cap_steps);
      CONFIG_INST.set_cap_steps(slew_cap_steps);

      const auto runtime_start = std::chrono::steady_clock::now();
      const auto result = icts::HTreeBuilder::build(matrix_result.selection.loads);
      const auto runtime_end = std::chrono::steady_clock::now();
      const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();

      const Arm9ExperimentRecord record
          = MakeArm9ExperimentRecord(wire_length_iterations, slew_cap_steps, runtime_s, result, matrix_result.selection.loads.size());
      matrix_result.records.push_back(record);
      AppendArm9CaseFailures(wire_length_iterations, slew_cap_steps, omit_wire_length_unit, runtime_s, result, record,
                             matrix_result.failure_messages);
    }
  }

  const std::string_view scenario_name = ResolveArm9MatrixScenarioName(omit_wire_length_unit);
  matrix_result.report_written = realtech_support::WriteScenarioLog(
      std::string(scenario_name), "matrix_report.txt",
      FormatArm9ExperimentReport(scenario_name, matrix_result.selection.clock_name, matrix_result.selection.loads.size(),
                                 omit_wire_length_unit, matrix_result.records));
  return matrix_result;
}

}  // namespace icts_test
