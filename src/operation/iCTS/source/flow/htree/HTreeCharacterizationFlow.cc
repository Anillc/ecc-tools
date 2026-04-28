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
 * @file HTreeCharacterizationFlow.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree characterization grid setup and CharBuilder result capture.
 */

#include <glog/logging.h>

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "CharBuilder.hh"
#include "Log.hh"
#include "LogFormat.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"
#include "logger/Schema.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree_builder {

auto RunCharacterizationFlow(const Tree& topology, int32_t dbu_per_um, const CharBuilder::InitOptions& base_char_options,
                             HTreeBuilder::BuildResult& result, CharBuilder& char_builder) -> HTreeCharacterizationFlowResult
{
  const auto char_grid_plan = ResolveCharacterizationGridPlan(topology, dbu_per_um);
  std::string grid_source = ToCharGridSourceName(CharGridSource::kNone);
  if (char_grid_plan.adapted) {
    grid_source = ToCharGridSourceName(char_grid_plan.source);
  } else if (char_grid_plan.configured_wirelength_unit_um > 0.0) {
    grid_source = ToCharGridSourceName(CharGridSource::kRuntimeConfig);
  }

  std::vector<std::string> decision_flag_values;
  if (char_grid_plan.configured_wirelength_missing && char_grid_plan.configured_grid_collapsed) {
    decision_flag_values.emplace_back("missing_config");
    decision_flag_values.emplace_back("collapsed_bins");
  } else if (char_grid_plan.configured_wirelength_missing) {
    decision_flag_values.emplace_back("missing_config");
  } else if (char_grid_plan.configured_grid_collapsed) {
    decision_flag_values.emplace_back("collapsed_bins");
  }
  if (char_grid_plan.adapted && char_grid_plan.wirelength_iterations < char_grid_plan.required_covering_iterations) {
    decision_flag_values.emplace_back("direct_bins_capped");
  }
  const std::string decision_flags = decision_flag_values.empty() ? std::string{"none"} : logformat::JoinStrings(decision_flag_values, "+");
  logformat::TableRows grid_plan_rows = {
      {"source", grid_source, char_grid_plan.adapted ? "fallback derived from topology level lengths" : "use runtime-configured grid"},
      {"requested_level_lengths", std::to_string(char_grid_plan.requested_level_lengths),
       "average parent-child segment length per topology level"},
      {"required_covering_iterations", std::to_string(char_grid_plan.required_covering_iterations),
       char_grid_plan.adapted ? "cover all topology level lengths under the resolved CharBuilder unit" : "0 (disabled)"},
      {"direct_characterization_bins", std::to_string(char_grid_plan.wirelength_iterations),
       char_grid_plan.adapted ? "direct-char bins after runtime cap" : "0 (disabled)"},
      {"distinct_level_bins", std::to_string(char_grid_plan.unique_level_bins), "aligned-length bins under resolved setup"},
      {"decision_flags", decision_flags, "fallback/adaptation trigger flags"},
  };
  if (char_grid_plan.adapted) {
    grid_plan_rows.insert(grid_plan_rows.begin() + 2,
                          {"resolved_wirelength_unit", logformat::FormatWithUnit(char_grid_plan.wirelength_unit_um, "um"),
                           "effective unit for the adapted characterization grid"});
  }
  SCHEMA_WRITER_INST.emitSection("### H-Tree Characterization");
  LogInfoTable("HTreeBuilder Characterization Grid Plan", {"Item", "Value", "Detail"}, grid_plan_rows);

  auto char_options = base_char_options;
  if (char_grid_plan.adapted) {
    char_options.wirelength_unit_um = char_grid_plan.wirelength_unit_um;
    char_options.wirelength_iterations = char_grid_plan.wirelength_iterations;
    auto direct_length_indices = ResolveDirectCharacterizationLengthIndices(topology, char_grid_plan, dbu_per_um);
    if (!direct_length_indices.empty()) {
      char_options.wirelength_indices = std::move(direct_length_indices);
    }
  }
  char_builder.init(char_options);
  char_builder.build();

  const double length_step_um = char_builder.get_wirelength_unit_um();
  if (length_step_um <= 0.0 || char_builder.get_segment_chars().empty()) {
    LOG_WARNING << "HTreeBuilder: characterization did not produce usable segment chars.";
    return HTreeCharacterizationFlowResult{.success = false, .failure_reason = "no_usable_segment_chars", .length_step_um = length_step_um};
  }

  result.char_wirelength_unit_um = length_step_um;
  result.char_wirelength_iterations = char_builder.get_wirelength_iterations();
  result.char_unique_level_bins = char_grid_plan.adapted
                                      ? char_grid_plan.unique_level_bins
                                      : CountUniqueAlignedLengthBins(CollectRequestedLevelLengthsUm(topology, dbu_per_um), length_step_um);
  result.char_grid_adapted = char_grid_plan.adapted;
  result.char_max_slew_ns = char_builder.get_max_slew();
  result.char_max_cap_pf = char_builder.get_max_cap();
  result.char_slew_steps = char_builder.get_slew_steps();
  result.char_cap_steps = char_builder.get_cap_steps();
  const logformat::TableRows char_summary_rows = {
      {"characterization_setup_source", "CharBuilder Setup", "resolved limits, wirelength lattice, buffers, and routing source"},
      {"characterization_results_source", "CharBuilder Results",
       "sample counts and overflow ratios are logged by the characterization owner"},
      {"wirelength_setup_source", char_grid_plan.adapted ? "HTreeBuilder Characterization Grid Plan" : "runtime_setup",
       char_grid_plan.adapted ? "effective wirelength lattice came from topology-grid adaptation"
                              : "runtime config supplied the characterization lattice"},
      {"grid_plan_source", grid_source, char_grid_plan.adapted ? "HTreeBuilder adapted characterization grid" : "no HTree override"},
  };
  LogInfoTable("HTreeBuilder Characterization Summary", {"Item", "Value", "Detail"}, char_summary_rows);

  return HTreeCharacterizationFlowResult{.success = true, .failure_reason = {}, .length_step_um = length_step_um};
}

}  // namespace icts::htree_builder
