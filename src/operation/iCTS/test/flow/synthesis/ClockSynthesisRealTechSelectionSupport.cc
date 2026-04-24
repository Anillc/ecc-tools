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
 * @file ClockSynthesisRealTechSelectionSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech clock selection and matrix report helpers for ClockSynthesis tests.
 */

#include <algorithm>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClockSynthesisRealTechSmokeSupport.hh"
#include "Tree.hh"
#include "common/io/TestArtifactIO.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "htree/HTreeBuilder.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::synthesis_realtech_smoke {
namespace {

auto SampleLoadsForSmoke(const std::vector<icts::Pin*>& loads, std::size_t max_count) -> std::vector<icts::Pin*>
{
  if (loads.size() <= max_count) {
    return loads;
  }

  std::vector<icts::Pin*> sampled_loads;
  sampled_loads.reserve(max_count);
  for (std::size_t sample_index = 0; sample_index < max_count; ++sample_index) {
    const std::size_t source_index = sample_index * loads.size() / max_count;
    sampled_loads.push_back(loads.at(source_index));
  }
  return sampled_loads;
}

auto ResolveBufferDriveCap(const std::string& cell_master) -> double
{
  double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (drive_cap_pf <= 0.0) {
    drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return drive_cap_pf;
}

}  // namespace

auto FormatClockSynthesisExperimentReport(std::string_view scenario_name, const RealClockSelection& selection, bool omit_wire_length_unit,
                                          const std::vector<ClockSynthesisExperimentRecord>& records) -> std::string
{
  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(6);
  report_stream << "scenario=" << scenario_name << "\n";
  report_stream << "clock_name=" << selection.clock_name << "\n";
  report_stream << "clock_net=" << selection.net_name << "\n";
  report_stream << "sink_count=" << selection.sinks.size() << "\n";
  report_stream << "sink_clustering_enabled=false\n";
  report_stream << "omit_wire_length_unit=" << (omit_wire_length_unit ? "true" : "false") << "\n";
  report_stream << "runtime_budget_s=" << kArm9SynthesisRuntimeBudgetS << "\n";
  report_stream << "columns=iter,step,runtime_s,success,frontier_count,selected_depth,best_pattern_id,best_delay_ns,best_power_w,"
                   "char_wire_length_unit_um,char_wire_length_iterations,char_grid_adapted,used_boundary_fallback,failure_reason\n";
  for (const auto& record : records) {
    report_stream << record.wire_length_iterations << "," << record.slew_cap_steps << "," << record.runtime_s << ","
                  << (record.success ? "true" : "false") << "," << record.final_frontier_count << "," << record.selected_depth << ","
                  << record.best_pattern_id << "," << record.best_delay_ns << "," << record.best_power_w << ","
                  << record.char_wire_length_unit_um << "," << record.char_wire_length_iterations << ","
                  << (record.char_grid_adapted ? "true" : "false") << "," << (record.used_boundary_fallback ? "true" : "false") << ","
                  << record.failure_reason << "\n";
  }
  return report_stream.str();
}

auto WriteClockSynthesisMatrixReport(std::string_view scenario_name, const std::string& file_name, const std::string& content) -> bool
{
  const auto output_dir = common::io::PrepareCleanOutputDir(common::io::ResolveOutputDir() / "flow" / "synthesis"
                                                            / common::io::SanitizeOutputName(std::string(scenario_name)));
  if (output_dir.empty()) {
    return false;
  }
  return common::io::WriteTextLog(output_dir / file_name, content);
}

auto SelectLargestRealClock(std::size_t max_count, std::size_t min_required_load_count) -> std::optional<RealClockSelection>
{
  DESIGN_INST.reset();
  STA_ADAPTER_INST.updateTiming();

  for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
    auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
    DESIGN_INST.add_clock(std::move(clock));
  }

  WRAPPER_INST.read();

  RealClockSelection best_selection;
  std::size_t best_source_load_count = 0U;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr || clock->get_clock_source() == nullptr || clock->get_loads().size() < min_required_load_count) {
      continue;
    }
    if (clock->get_loads().size() <= best_source_load_count) {
      continue;
    }

    best_selection.clock_name = clock->get_clock_name();
    best_selection.net_name = clock->get_clock_net_name();
    best_selection.source = clock->get_clock_source();
    best_selection.sinks = SampleLoadsForSmoke(clock->get_loads(), max_count);
    best_source_load_count = clock->get_loads().size();
  }

  if (best_selection.source == nullptr || best_selection.sinks.size() < min_required_load_count) {
    return std::nullopt;
  }
  return best_selection;
}

auto SetEnableSinkClustering(icts::ClockSynthesis::BuildOptions& options, bool enabled) -> void
{
  options.enable_sink_clustering = enabled;
}

auto ResolveExpectedMinClusterBufferMaster() -> std::optional<std::string>
{
  std::optional<std::string> expected_master = std::nullopt;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (cell_master.empty()) {
      continue;
    }

    const auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (input_pin.empty() || output_pin.empty()) {
      continue;
    }

    const double drive_cap_pf = ResolveBufferDriveCap(cell_master);
    if (drive_cap_pf <= 0.0) {
      continue;
    }

    if (!expected_master.has_value() || drive_cap_pf < best_drive_cap_pf
        || (drive_cap_pf == best_drive_cap_pf && cell_master < *expected_master)) {
      expected_master = cell_master;
      best_drive_cap_pf = drive_cap_pf;
    }
  }

  return expected_master;
}

auto CalcFloorPowerOfTwo(std::size_t value) -> std::size_t
{
  if (value == 0U) {
    return 0U;
  }

  std::size_t power = 1U;
  while ((power << 1U) <= value) {
    power <<= 1U;
  }
  return power;
}

auto CountTopologyLeafNodes(const icts::Tree& topology) -> std::size_t
{
  const auto levels = topology.levels();
  if (levels.empty()) {
    return 0U;
  }
  return levels.back().size();
}

auto FindSelectedDepthSummary(const icts::HTreeBuilder::BuildResult& result)
    -> const icts::HTreeBuilder::BuildResult::DepthCandidateSummary*
{
  const auto summary_it = std::ranges::find_if(result.depth_candidates, [](const auto& summary) -> bool { return summary.selected; });
  if (summary_it == result.depth_candidates.end()) {
    return nullptr;
  }
  return &(*summary_it);
}

}  // namespace icts_test::synthesis_realtech_smoke
