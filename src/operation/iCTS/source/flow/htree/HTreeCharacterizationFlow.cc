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

#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "Log.hh"
#include "LogFormat.hh"
#include "SegmentChar.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree_builder {

auto RunCharacterizationFlow(const Tree& topology, int32_t dbu_per_um, HTreeBuilder::BuildResult& result, CharBuilder& char_builder)
    -> HTreeCharacterizationFlowResult
{
  const auto char_grid_plan = ResolveCharacterizationGridPlan(topology, dbu_per_um);
  std::string grid_source = ToCharGridSourceName(CharGridSource::kNone);
  if (char_grid_plan.adapted) {
    grid_source = ToCharGridSourceName(char_grid_plan.source);
  } else if (char_grid_plan.configured_wire_length_unit_um > 0.0) {
    grid_source = ToCharGridSourceName(CharGridSource::kRuntimeConfig);
  }

  std::string grid_effective_unit = "unresolved";
  if (char_grid_plan.adapted) {
    grid_effective_unit = logformat::FormatWithUnit(char_grid_plan.wire_length_unit_um, "um");
  } else if (char_grid_plan.configured_wire_length_unit_um > 0.0) {
    grid_effective_unit = logformat::FormatWithUnit(char_grid_plan.configured_wire_length_unit_um, "um");
  }
  std::string decision_flags = "none";
  if (char_grid_plan.configured_wire_length_missing && char_grid_plan.configured_grid_collapsed) {
    decision_flags = "missing_config+collapsed_bins";
  } else if (char_grid_plan.configured_wire_length_missing) {
    decision_flags = "missing_config";
  } else if (char_grid_plan.configured_grid_collapsed) {
    decision_flags = "collapsed_bins";
  }
  const logformat::TableRows grid_plan_rows = {
      {"source", grid_source, char_grid_plan.adapted ? "fallback derived from topology level lengths" : "use runtime-configured grid"},
      {"requested_level_lengths", std::to_string(char_grid_plan.requested_level_lengths),
       "average parent-child segment length per topology level"},
      {"configured_wire_length_unit_um",
       char_grid_plan.configured_wire_length_unit_um > 0.0 ? logformat::FormatWithUnit(char_grid_plan.configured_wire_length_unit_um, "um")
                                                           : std::string{"auto"},
       char_grid_plan.configured_wire_length_missing ? "missing" : "present"},
      {"configured_wire_length_iterations", std::to_string(char_grid_plan.configured_wire_length_iterations),
       "hard cap for direct characterization length bins"},
      {"effective_wire_length_unit_um", grid_effective_unit,
       char_grid_plan.adapted ? "caller override for characterization" : "no override"},
      {"required_covering_iterations", std::to_string(char_grid_plan.required_covering_iterations),
       char_grid_plan.adapted ? "cover all topology level lengths under effective unit" : "0 (disabled)"},
      {"wire_length_iterations_override", std::to_string(char_grid_plan.wire_length_iterations),
       char_grid_plan.adapted ? "effective direct-char upper bound after cap" : "0 (disabled)"},
      {"distinct_level_bins", std::to_string(char_grid_plan.unique_level_bins), "aligned-length bins under effective unit"},
      {"decision_flags", decision_flags, "fallback trigger diagnostics"},
  };
  LogInfoTable("HTreeBuilder Characterization Grid Plan", {"Item", "Value", "Detail"}, grid_plan_rows);

  CharBuilder::InitOptions char_options;
  if (char_grid_plan.adapted) {
    char_options.wire_length_unit_um = char_grid_plan.wire_length_unit_um;
    char_options.wire_length_iterations = char_grid_plan.wire_length_iterations;
    auto direct_length_indices = ResolveDirectCharacterizationLengthIndices(topology, char_grid_plan, dbu_per_um);
    if (!direct_length_indices.empty()) {
      char_options.wire_length_indices = std::move(direct_length_indices);
    }
  }
  char_builder.init(char_options);
  char_builder.build();

  const double length_step_um = char_builder.get_wire_length_unit_um();
  if (length_step_um <= 0.0 || char_builder.get_segment_chars().empty()) {
    LOG_WARNING << "HTreeBuilder: characterization did not produce usable segment chars.";
    return HTreeCharacterizationFlowResult{.success = false, .failure_reason = "no_usable_segment_chars", .length_step_um = length_step_um};
  }

  result.char_wire_length_unit_um = length_step_um;
  result.char_wire_length_iterations = char_builder.get_wire_length_iterations();
  result.char_unique_level_bins = char_grid_plan.adapted
                                      ? char_grid_plan.unique_level_bins
                                      : CountUniqueAlignedLengthBins(CollectRequestedLevelLengthsUm(topology, dbu_per_um), length_step_um);
  result.char_grid_adapted = char_grid_plan.adapted;
  result.char_max_slew_ns = char_builder.get_max_slew();
  result.char_max_cap_pf = char_builder.get_max_cap();
  result.char_slew_steps = char_builder.get_slew_steps();
  result.char_cap_steps = char_builder.get_cap_steps();
  const std::string char_wire_length_source
      = char_builder.get_wire_length_unit_source().empty() ? std::string{"unresolved"} : char_builder.get_wire_length_unit_source();
  const std::string char_wire_length_detail = char_builder.get_wire_length_unit_detail().empty()
                                                  ? std::string{"no resolution detail"}
                                                  : char_builder.get_wire_length_unit_detail();
  const logformat::TableRows char_summary_rows = {
      {"wire_length_unit_um", logformat::FormatWithUnit(result.char_wire_length_unit_um, "um"), char_wire_length_source},
      {"wire_length_unit_detail", char_wire_length_detail,
       char_grid_plan.adapted ? "effective value under HTree build override" : "direct CharBuilder resolution"},
      {"grid_plan_source", grid_source, char_grid_plan.adapted ? "HTreeBuilder adapted characterization grid" : "no HTree override"},
      {"wire_length_iterations", std::to_string(result.char_wire_length_iterations), "characterization sweep length bins"},
      {"max_slew_ns", logformat::FormatWithUnit(result.char_max_slew_ns, "ns"), "characterization upper bound"},
      {"max_cap_pf", logformat::FormatWithUnit(result.char_max_cap_pf, "pF"), "characterization upper bound"},
      {"segment_chars", std::to_string(char_builder.get_segment_chars().size()), "raw segment characterization entries"},
      {"buffer_patterns", std::to_string(char_builder.get_buffering_patterns().size()), "raw segment pattern count"},
  };
  LogInfoTable("HTreeBuilder Characterization Summary", {"Item", "Value", "Detail"}, char_summary_rows);

  return HTreeCharacterizationFlowResult{.success = true, .failure_reason = {}, .length_step_um = length_step_um};
}

}  // namespace icts::htree_builder
