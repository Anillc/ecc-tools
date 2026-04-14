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
 * @file CharacterizationRealTechTestSupport.hh
 * @brief Shared helpers for real-tech characterization tests.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/characterization/CharCore.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/config/Config.hh"
#include "module/characterization/CharBuilder.hh"
#include "module/characterization/HTreeTopologyCharTable.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/Pruner.hh"
#include "module/characterization/SegmentCharTable.hh"

namespace icts_test::characterization::realtech {

inline constexpr std::size_t kReportExampleLimit = 3U;
inline constexpr std::size_t kRelaxedCandidatesPerBoundaryGroup = 2U;
inline constexpr double kLeafLevelLengthUm = 50.0;
inline constexpr double kMidLevelLengthUm = 100.0;
inline constexpr double kRootLevelLengthUm = 200.0;
inline constexpr double kExactLeafLevelLengthUm = 25.0;
inline constexpr double kExactMidLevelLengthUm = 50.0;
inline constexpr double kExactRootLevelLengthUm = 100.0;
inline constexpr double kRealTechCharWireLengthUnitUm = 25.0;
inline constexpr unsigned kRealTechCharWireLengthIterations = 5U;
inline constexpr unsigned kRealTechCharSlewSteps = 5U;
inline constexpr unsigned kRealTechCharCapSteps = 5U;
inline constexpr unsigned kRealTechCharMaxPatternNodes = 5U;

using common::realtech::RealTechMode;

struct ConfigState
{
  double skew_bound = 0.0;
  double max_buf_tran = 0.0;
  double max_sink_tran = 0.0;
  double max_cap = 0.0;
  bool has_max_buf_tran = false;
  bool has_max_cap = false;
  double max_length = 0.0;
  double wire_length_unit_um = 0.0;
  unsigned wire_length_iterations = 0U;
  unsigned slew_steps = 0U;
  unsigned cap_steps = 0U;
  unsigned max_pattern_nodes = 0U;
  double wire_width = 0.0;
  unsigned max_fanout = 0U;
  std::vector<unsigned> routing_layers;
  std::vector<std::string> buffer_types;
  double char_buf_redundancy_pct = 0.0;
  std::string work_dir;
  std::string output_def_path;
  std::string log_file;
  std::string gds_file;
  bool use_netlist = false;
  std::vector<std::pair<std::string, std::string>> net_list;
};

inline auto CaptureConfigState() -> ConfigState
{
  ConfigState state{};
  state.skew_bound = CONFIG_INST.get_skew_bound();
  state.max_buf_tran = CONFIG_INST.get_max_buf_tran();
  state.max_sink_tran = CONFIG_INST.get_max_sink_tran();
  state.max_cap = CONFIG_INST.get_max_cap();
  state.has_max_buf_tran = CONFIG_INST.has_max_buf_tran();
  state.has_max_cap = CONFIG_INST.has_max_cap();
  state.max_length = CONFIG_INST.get_max_length();
  state.wire_length_unit_um = CONFIG_INST.get_wire_length_unit_um();
  state.wire_length_iterations = CONFIG_INST.get_wire_length_iterations();
  state.slew_steps = CONFIG_INST.get_slew_steps();
  state.cap_steps = CONFIG_INST.get_cap_steps();
  state.max_pattern_nodes = CONFIG_INST.get_max_pattern_nodes();
  state.wire_width = CONFIG_INST.get_wire_width();
  state.max_fanout = CONFIG_INST.get_max_fanout();
  state.routing_layers = CONFIG_INST.get_routing_layers();
  state.buffer_types = CONFIG_INST.get_buffer_types();
  state.char_buf_redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();
  state.work_dir = CONFIG_INST.get_work_dir();
  state.output_def_path = CONFIG_INST.get_output_def_path();
  state.log_file = CONFIG_INST.get_log_file();
  state.gds_file = CONFIG_INST.get_gds_file();
  state.use_netlist = CONFIG_INST.is_use_netlist();
  state.net_list = CONFIG_INST.get_net_list();
  return state;
}

inline auto ApplyConfigState(const ConfigState& state) -> void
{
  CONFIG_INST.reset();
  CONFIG_INST.set_skew_bound(state.skew_bound);
  if (state.has_max_buf_tran) {
    CONFIG_INST.set_max_buf_tran(state.max_buf_tran);
  }
  CONFIG_INST.set_max_sink_tran(state.max_sink_tran);
  if (state.has_max_cap) {
    CONFIG_INST.set_max_cap(state.max_cap);
  }
  CONFIG_INST.set_max_length(state.max_length);
  CONFIG_INST.set_wire_length_unit_um(state.wire_length_unit_um);
  CONFIG_INST.set_wire_length_iterations(state.wire_length_iterations);
  CONFIG_INST.set_slew_steps(state.slew_steps);
  CONFIG_INST.set_cap_steps(state.cap_steps);
  CONFIG_INST.set_max_pattern_nodes(state.max_pattern_nodes);
  CONFIG_INST.set_wire_width(state.wire_width);
  CONFIG_INST.set_max_fanout(state.max_fanout);
  CONFIG_INST.set_routing_layers(state.routing_layers);
  CONFIG_INST.set_buffer_types(state.buffer_types);
  CONFIG_INST.set_char_buf_redundancy_pct(state.char_buf_redundancy_pct);
  CONFIG_INST.set_work_dir(state.work_dir);
  CONFIG_INST.set_output_def_path(state.output_def_path);
  CONFIG_INST.set_log_file(state.log_file);
  CONFIG_INST.set_gds_file(state.gds_file);
  CONFIG_INST.set_use_netlist(state.use_netlist);
  CONFIG_INST.set_net_list(state.net_list);
}

inline auto MakeRealTechCharConfigState(const ConfigState& baseline_state, std::optional<std::vector<std::string>> buffer_types,
                                        double max_buf_tran_ns, double max_cap_pf, bool omit_wire_length_unit) -> ConfigState
{
  auto configured_state = baseline_state;
  configured_state.has_max_buf_tran = max_buf_tran_ns > 0.0;
  configured_state.max_buf_tran = max_buf_tran_ns;
  configured_state.has_max_cap = max_cap_pf > 0.0;
  configured_state.max_cap = max_cap_pf;
  configured_state.wire_length_unit_um = omit_wire_length_unit ? 0.0 : kRealTechCharWireLengthUnitUm;
  configured_state.wire_length_iterations = kRealTechCharWireLengthIterations;
  configured_state.slew_steps = kRealTechCharSlewSteps;
  configured_state.cap_steps = kRealTechCharCapSteps;
  configured_state.max_pattern_nodes = kRealTechCharMaxPatternNodes;
  configured_state.char_buf_redundancy_pct = 0.0;
  if (buffer_types.has_value()) {
    configured_state.buffer_types = *buffer_types;
  }
  return configured_state;
}

struct RealTechCharSession
{
  ~RealTechCharSession() { restore(); }

  auto prepare(const std::string& scenario_name, std::optional<std::vector<std::string>> buffer_types, double max_buf_tran_ns,
               double max_cap_pf, bool omit_wire_length_unit = false) -> std::optional<std::string>
  {
    (void) scenario_name;
    if (_is_prepared) {
      restore();
    }

    const auto& setup_state = common::realtech::EnsureRealTechSetup();
    if (setup_state.mode != RealTechMode::kRealTech || !setup_state.setup_succeeded) {
      return setup_state.summary;
    }

    const auto& cts_config_path = setup_state.cts_config_path;
    if (cts_config_path.empty() || !std::filesystem::exists(cts_config_path)) {
      return "Cannot resolve real-tech CTS config path from setup state.";
    }

    const auto original_config_state = CaptureConfigState();
    auto configured_state
        = MakeRealTechCharConfigState(original_config_state, std::move(buffer_types), max_buf_tran_ns, max_cap_pf, omit_wire_length_unit);
    _original_config_state = original_config_state;
    ApplyConfigState(configured_state);
    STA_ADAPTER_INST.initCharOnly();
    _is_prepared = true;
    return std::nullopt;
  }

 private:
  auto restore() -> void
  {
    if (!_is_prepared || !_original_config_state.has_value()) {
      return;
    }

    ApplyConfigState(*_original_config_state);
    STA_ADAPTER_INST.init();
    _is_prepared = false;
    _original_config_state.reset();
  }

  bool _is_prepared = false;
  std::optional<ConfigState> _original_config_state;
};

struct BufferLimitInfo
{
  std::string cell_master;
  std::string input_pin;
  std::string output_pin;
  double port_slew_limit_ns = 0.0;
  double table_slew_limit_ns = 0.0;
  double port_cap_limit_pf = 0.0;
  double table_cap_limit_pf = 0.0;
};

struct CharGrid
{
  double length_step_um = 0.0;
  double slew_step_ns = 0.0;
  double cap_step_pf = 0.0;
};

struct HTreeStageSummary
{
  std::string label;
  std::vector<icts::HTreeTopologyChar> raw_entries;
  std::vector<icts::HTreeTopologyChar> frontier_entries;
};

inline auto JoinStrings(const std::vector<std::string>& values) -> std::string
{
  std::ostringstream output_stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output_stream << ",";
    }
    output_stream << values.at(index);
  }
  return output_stream.str();
}

inline auto WriteScenarioLog(const std::string& scenario_name, const std::string& file_name, const std::string& content) -> bool
{
  const auto output_dir = icts_test::common::io::ResolveOutputDir() / "characterization" / "realtech" / scenario_name;
  std::error_code error_code;
  std::filesystem::create_directories(output_dir, error_code);
  if (error_code) {
    return false;
  }
  return icts_test::common::io::WriteTextLog(output_dir / file_name, content);
}

inline auto CollectConfiguredBufferLimitInfo() -> std::vector<BufferLimitInfo>
{
  std::vector<BufferLimitInfo> infos;
  std::set<std::string> seen_cell_masters;

  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (!seen_cell_masters.insert(cell_master).second) {
      continue;
    }

    auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (input_pin.empty() || output_pin.empty()) {
      continue;
    }

    infos.push_back(BufferLimitInfo{
        .cell_master = cell_master,
        .input_pin = std::move(input_pin),
        .output_pin = std::move(output_pin),
        .port_slew_limit_ns = STA_ADAPTER_INST.queryCellInPinSlewLimit(cell_master),
        .table_slew_limit_ns = STA_ADAPTER_INST.queryCellInPinSlewTableAxisMax(cell_master),
        .port_cap_limit_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master),
        .table_cap_limit_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master),
    });
  }

  return infos;
}

inline auto CollectUsableBufferMasters(const std::vector<BufferLimitInfo>& infos) -> std::vector<std::string>
{
  std::vector<std::string> masters;
  for (const auto& info : infos) {
    const bool has_slew_support = info.port_slew_limit_ns > 0.0 || info.table_slew_limit_ns > 0.0;
    const bool has_cap_support = info.port_cap_limit_pf > 0.0 || info.table_cap_limit_pf > 0.0;
    if (has_slew_support && has_cap_support) {
      masters.push_back(info.cell_master);
    }
  }
  return masters;
}

inline auto LookupBufferInfo(const std::vector<BufferLimitInfo>& infos, const std::string& cell_master) -> const BufferLimitInfo*
{
  auto it = std::ranges::find_if(infos, [&cell_master](const BufferLimitInfo& info) -> bool { return info.cell_master == cell_master; });
  return it == infos.end() ? nullptr : &(*it);
}

inline auto MinPositiveResolvedLimit(const std::vector<BufferLimitInfo>& infos, const std::vector<std::string>& selected_masters,
                                     bool for_slew) -> double
{
  double port_min = std::numeric_limits<double>::infinity();
  double table_min = std::numeric_limits<double>::infinity();

  for (const auto& cell_master : selected_masters) {
    const auto* info = LookupBufferInfo(infos, cell_master);
    if (info == nullptr) {
      continue;
    }

    const double port_value = for_slew ? info->port_slew_limit_ns : info->port_cap_limit_pf;
    const double table_value = for_slew ? info->table_slew_limit_ns : info->table_cap_limit_pf;
    if (port_value > 0.0) {
      port_min = std::min(port_min, port_value);
    }
    if (table_value > 0.0) {
      table_min = std::min(table_min, table_value);
    }
  }

  if (std::isfinite(port_min)) {
    return port_min;
  }
  if (std::isfinite(table_min)) {
    return table_min;
  }
  return 0.0;
}

inline auto ResolveDefaultWireLengthUnitUm(const std::vector<BufferLimitInfo>& infos, const std::vector<std::string>& selected_masters)
    -> double
{
  // Mirror CharBuilder fallback so the test expectation stays aligned with production.
  double strongest_drive_cap_pf = -1.0;
  double resolved_unit_um = 0.0;

  for (const auto& cell_master : selected_masters) {
    const auto* info = LookupBufferInfo(infos, cell_master);
    if (info == nullptr) {
      continue;
    }

    const double drive_cap_pf = info->port_cap_limit_pf > 0.0 ? info->port_cap_limit_pf : info->table_cap_limit_pf;
    if (drive_cap_pf <= 0.0) {
      continue;
    }

    const double cell_height_um = STA_ADAPTER_INST.queryCellHeightUm(cell_master);
    if (cell_height_um <= 0.0) {
      continue;
    }

    if (drive_cap_pf > strongest_drive_cap_pf) {
      strongest_drive_cap_pf = drive_cap_pf;
      resolved_unit_um = cell_height_um * 10.0;
    }
  }

  return resolved_unit_um;
}

template <class PredicateT>
inline auto CollectMastersByPredicate(const std::vector<BufferLimitInfo>& infos, const PredicateT& predicate) -> std::vector<std::string>
{
  std::vector<std::string> masters;
  for (const auto& info : infos) {
    if (predicate(info)) {
      masters.push_back(info.cell_master);
    }
  }
  return masters;
}

template <class CharT>
inline auto SortCharsForReport(std::vector<CharT>& chars) -> void
{
  std::ranges::sort(chars, [](const CharT& lhs, const CharT& rhs) -> bool {
    if (lhs.get_input_slew_idx() != rhs.get_input_slew_idx()) {
      return lhs.get_input_slew_idx() < rhs.get_input_slew_idx();
    }
    if (lhs.get_driven_cap_idx() != rhs.get_driven_cap_idx()) {
      return lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx();
    }
    if (lhs.get_output_slew_idx() != rhs.get_output_slew_idx()) {
      return lhs.get_output_slew_idx() < rhs.get_output_slew_idx();
    }
    if (lhs.get_load_cap_idx() != rhs.get_load_cap_idx()) {
      return lhs.get_load_cap_idx() > rhs.get_load_cap_idx();
    }
    if (lhs.get_delay() != rhs.get_delay()) {
      return lhs.get_delay() < rhs.get_delay();
    }
    if (lhs.get_power() != rhs.get_power()) {
      return lhs.get_power() < rhs.get_power();
    }
    return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
  });
}

template <class CharT>
inline auto BuildInputBoundaryFrontier(const std::vector<CharT>& chars) -> std::vector<CharT>
{
  const icts::InputBoundaryPruner<CharT> pruner;
  std::unordered_map<unsigned, std::vector<const CharT*>> grouped_entries;
  grouped_entries.reserve(chars.size());

  for (const auto& entry : chars) {
    grouped_entries[pruner.groupKey(entry)].push_back(&entry);
  }

  std::vector<CharT> frontier_entries;
  frontier_entries.reserve(chars.size());
  for (const auto& [group_key, entries] : grouped_entries) {
    (void) group_key;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      bool dominated = false;
      for (std::size_t other_index = 0; other_index < entries.size(); ++other_index) {
        if (index == other_index) {
          continue;
        }
        if (pruner.dominates(*entries.at(other_index), *entries.at(index))) {
          dominated = true;
          break;
        }
      }
      if (!dominated) {
        frontier_entries.push_back(*entries.at(index));
      }
    }
  }

  SortCharsForReport(frontier_entries);
  return frontier_entries;
}

template <class CharT>
inline auto SelectCompositionCandidates(const std::vector<CharT>& entries, std::size_t max_per_boundary_group) -> std::vector<CharT>
{
  const icts::InputBoundaryPruner<CharT> pruner;
  std::unordered_map<unsigned, std::size_t> group_counts;
  group_counts.reserve(entries.size());

  std::vector<CharT> selected_entries;
  selected_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    const unsigned group_key = pruner.groupKey(entry);
    auto& kept_count = group_counts[group_key];
    if (kept_count >= max_per_boundary_group) {
      continue;
    }
    selected_entries.push_back(entry);
    ++kept_count;
  }
  return selected_entries;
}

inline auto MakeLengthIndex(double length_um, double length_step_um) -> unsigned
{
  if (length_step_um <= 0.0) {
    return 0U;
  }
  return static_cast<unsigned>(std::lround(length_um / length_step_um));
}

inline auto CalcCharGrid(const icts::CharBuilder& builder) -> CharGrid
{
  return CharGrid{
      .length_step_um = builder.get_wire_length_unit_um(),
      .slew_step_ns = builder.get_slew_steps() == 0U ? 0.0 : builder.get_max_slew() / static_cast<double>(builder.get_slew_steps()),
      .cap_step_pf = builder.get_cap_steps() == 0U ? 0.0 : builder.get_max_cap() / static_cast<double>(builder.get_cap_steps()),
  };
}

inline auto MakeSegmentCharTable(const std::vector<icts::SegmentChar>& chars) -> icts::SegmentCharTable
{
  icts::SegmentCharTable table;
  table.reserve(chars.size());
  for (const auto& entry : chars) {
    table.addChar(entry);
  }
  return table;
}

inline auto MakeHTreeCharTable(const std::vector<icts::HTreeTopologyChar>& chars) -> icts::HTreeTopologyCharTable
{
  icts::HTreeTopologyCharTable table;
  table.reserve(chars.size());
  for (const auto& entry : chars) {
    table.addChar(entry);
  }
  return table;
}

inline auto FindNextSegmentPatternId(const std::vector<icts::SegmentChar>& chars) -> unsigned
{
  unsigned next_id = 0U;
  for (const auto& entry : chars) {
    next_id = std::max(next_id, entry.get_pattern_id().local_id + 1U);
  }
  return next_id;
}

inline auto ComposeSegmentEntriesExact(const std::vector<icts::SegmentChar>& upstream, const std::vector<icts::SegmentChar>& downstream,
                                       unsigned& next_pattern_id) -> std::vector<icts::SegmentChar>
{
  icts::SegmentPatternCombiner combiner(next_pattern_id);
  auto composed_entries = MakeSegmentCharTable(upstream).concatWith(MakeSegmentCharTable(downstream), combiner).get_chars();
  next_pattern_id = combiner.get_next_id();
  SortCharsForReport(composed_entries);
  return composed_entries;
}

inline auto ComposeSegmentEntriesRelaxed(const std::vector<icts::SegmentChar>& upstream, const std::vector<icts::SegmentChar>& downstream,
                                         unsigned& next_pattern_id) -> std::vector<icts::SegmentChar>
{
  icts::SegmentPatternCombiner combiner(next_pattern_id);
  std::vector<icts::SegmentChar> composed_entries;
  composed_entries.reserve(upstream.size() * downstream.size());
  for (const auto& upstream_entry : upstream) {
    for (const auto& downstream_entry : downstream) {
      if (downstream_entry.get_input_slew_idx() < upstream_entry.get_output_slew_idx()
          || upstream_entry.get_load_cap_idx() < downstream_entry.get_driven_cap_idx()) {
        continue;
      }
      const auto merged_pattern_id = combiner.combine(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id());
      composed_entries.push_back(icts::SegmentChar::compose(upstream_entry, downstream_entry, merged_pattern_id));
    }
  }

  next_pattern_id = combiner.get_next_id();
  SortCharsForReport(composed_entries);
  return composed_entries;
}

inline auto BuildSegmentLengthFrontiers(const std::vector<icts::SegmentChar>& chars)
    -> std::unordered_map<unsigned, std::vector<icts::SegmentChar>>
{
  std::unordered_map<unsigned, std::vector<icts::SegmentChar>> raw_by_length;
  raw_by_length.reserve(chars.size());
  for (const auto& entry : chars) {
    raw_by_length[entry.get_length_idx()].push_back(entry);
  }

  std::unordered_map<unsigned, std::vector<icts::SegmentChar>> frontier_by_length;
  frontier_by_length.reserve(raw_by_length.size());
  for (auto& [length_idx, raw_entries] : raw_by_length) {
    (void) length_idx;
    frontier_by_length[length_idx] = BuildInputBoundaryFrontier(raw_entries);
  }

  return frontier_by_length;
}

inline auto SynthesizeSegmentFrontierExactOnly(std::unordered_map<unsigned, std::vector<icts::SegmentChar>>& frontier_by_length,
                                               unsigned target_length_idx, unsigned& next_segment_pattern_id) -> bool
{
  for (unsigned length_idx = 1U; length_idx <= target_length_idx; ++length_idx) {
    if (frontier_by_length.contains(length_idx) && !frontier_by_length.at(length_idx).empty()) {
      continue;
    }

    std::vector<icts::SegmentChar> exact_composed_entries;
    for (unsigned left_idx = 1U; left_idx < length_idx; ++left_idx) {
      const unsigned right_idx = length_idx - left_idx;
      const auto left_it = frontier_by_length.find(left_idx);
      const auto right_it = frontier_by_length.find(right_idx);
      if (left_it == frontier_by_length.end() || right_it == frontier_by_length.end()) {
        continue;
      }
      if (left_it->second.empty() || right_it->second.empty()) {
        continue;
      }

      auto partial = ComposeSegmentEntriesExact(left_it->second, right_it->second, next_segment_pattern_id);
      exact_composed_entries.insert(exact_composed_entries.end(), partial.begin(), partial.end());
    }

    if (!exact_composed_entries.empty()) {
      frontier_by_length[length_idx] = BuildInputBoundaryFrontier(exact_composed_entries);
    }
  }

  return frontier_by_length.contains(target_length_idx) && !frontier_by_length.at(target_length_idx).empty();
}

inline auto SynthesizeSegmentFrontierIfMissing(std::unordered_map<unsigned, std::vector<icts::SegmentChar>>& frontier_by_length,
                                               unsigned target_length_idx, unsigned& next_segment_pattern_id) -> bool
{
  for (unsigned length_idx = 1U; length_idx <= target_length_idx; ++length_idx) {
    if (frontier_by_length.contains(length_idx) && !frontier_by_length.at(length_idx).empty()) {
      continue;
    }

    std::vector<icts::SegmentChar> exact_composed_entries;
    for (unsigned left_idx = 1U; left_idx < length_idx; ++left_idx) {
      const unsigned right_idx = length_idx - left_idx;
      const auto left_it = frontier_by_length.find(left_idx);
      const auto right_it = frontier_by_length.find(right_idx);
      if (left_it == frontier_by_length.end() || right_it == frontier_by_length.end()) {
        continue;
      }
      if (left_it->second.empty() || right_it->second.empty()) {
        continue;
      }

      auto partial = ComposeSegmentEntriesExact(left_it->second, right_it->second, next_segment_pattern_id);
      exact_composed_entries.insert(exact_composed_entries.end(), partial.begin(), partial.end());
    }

    if (!exact_composed_entries.empty()) {
      frontier_by_length[length_idx] = BuildInputBoundaryFrontier(exact_composed_entries);
      continue;
    }

    std::vector<icts::SegmentChar> relaxed_composed_entries;
    for (unsigned left_idx = 1U; left_idx < length_idx; ++left_idx) {
      const unsigned right_idx = length_idx - left_idx;
      const auto left_it = frontier_by_length.find(left_idx);
      const auto right_it = frontier_by_length.find(right_idx);
      if (left_it == frontier_by_length.end() || right_it == frontier_by_length.end()) {
        continue;
      }
      if (left_it->second.empty() || right_it->second.empty()) {
        continue;
      }

      const auto left_candidates = SelectCompositionCandidates(left_it->second, kRelaxedCandidatesPerBoundaryGroup);
      const auto right_candidates = SelectCompositionCandidates(right_it->second, kRelaxedCandidatesPerBoundaryGroup);
      auto partial = ComposeSegmentEntriesRelaxed(left_candidates, right_candidates, next_segment_pattern_id);
      relaxed_composed_entries.insert(relaxed_composed_entries.end(), partial.begin(), partial.end());
    }

    if (!relaxed_composed_entries.empty()) {
      frontier_by_length[length_idx] = BuildInputBoundaryFrontier(relaxed_composed_entries);
    }
  }

  return frontier_by_length.contains(target_length_idx) && !frontier_by_length.at(target_length_idx).empty();
}

inline auto MakeHTreeSeedEntries(const std::vector<icts::SegmentChar>& segment_frontier, unsigned& next_topology_pattern_id)
    -> std::vector<icts::HTreeTopologyChar>
{
  std::vector<icts::HTreeTopologyChar> seed_entries;
  seed_entries.reserve(segment_frontier.size());
  for (const auto& segment_entry : segment_frontier) {
    const auto topology_pattern_id = icts::PatternId::topology(next_topology_pattern_id++);
    const icts::CharCore core(segment_entry.get_input_slew_idx(), segment_entry.get_output_slew_idx(), segment_entry.get_driven_cap_idx(),
                              segment_entry.get_load_cap_idx(), segment_entry.get_delay(), segment_entry.get_power(), topology_pattern_id);
    seed_entries.emplace_back(core, 1U);
  }
  SortCharsForReport(seed_entries);
  return seed_entries;
}

inline auto ComposeHTreeEntriesExact(const std::vector<icts::HTreeTopologyChar>& upstream,
                                     const std::vector<icts::HTreeTopologyChar>& downstream, unsigned& next_topology_pattern_id)
    -> std::vector<icts::HTreeTopologyChar>
{
  icts::TopologyPatternCombiner combiner(next_topology_pattern_id);
  auto composed_entries = MakeHTreeCharTable(upstream).concatWith(MakeHTreeCharTable(downstream), combiner).get_chars();
  next_topology_pattern_id = combiner.get_next_id();
  SortCharsForReport(composed_entries);
  return composed_entries;
}

inline auto CountPositivePower(const std::vector<icts::SegmentChar>& chars) -> std::size_t
{
  return static_cast<std::size_t>(
      std::ranges::count_if(chars, [](const icts::SegmentChar& entry) -> bool { return entry.get_power() > 0.0; }));
}

inline auto AppendWireLengthStats(std::ostringstream& report_stream, const std::vector<icts::CharBuilder::WireLengthBuildStat>& stats)
    -> void
{
  for (const auto& stat : stats) {
    report_stream << "wire_length_stat{length_um=" << stat.wire_length_um << ",estimated_patterns=" << stat.estimated_patterns
                  << ",estimated_sta_samples=" << stat.estimated_sta_samples << ",evaluated_patterns=" << stat.evaluated_patterns
                  << ",feasible_patterns=" << stat.feasible_patterns << ",skipped_patterns=" << stat.skipped_patterns_infeasible
                  << ",skipped_load_points=" << stat.skipped_load_points << ",skipped_sta_samples=" << stat.skipped_sta_samples
                  << ",executed_sta_samples=" << stat.executed_sta_samples << ",added_segment_chars=" << stat.added_segment_chars
                  << ",added_patterns=" << stat.added_patterns << ",elapsed_ms=" << stat.elapsed_ms << "}\n";
  }
}

inline auto FormatSegmentChar(const icts::SegmentChar& entry, const CharGrid& grid) -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(3) << "{length_um=" << (entry.get_length_idx() * grid.length_step_um)
                << ", input_slew_ns=" << (entry.get_input_slew_idx() * grid.slew_step_ns)
                << ", output_slew_ns=" << (entry.get_output_slew_idx() * grid.slew_step_ns)
                << ", driven_cap_pf=" << (entry.get_driven_cap_idx() * grid.cap_step_pf)
                << ", load_cap_pf=" << (entry.get_load_cap_idx() * grid.cap_step_pf) << ", delay_ns=" << entry.get_delay();
  output_stream << ", power_w=" << std::scientific << std::setprecision(6) << entry.get_power() << "}";
  return output_stream.str();
}

inline auto FormatHTreeChar(const icts::HTreeTopologyChar& entry, const CharGrid& grid) -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(3) << "{levels=" << entry.get_levels()
                << ", input_slew_ns=" << (entry.get_input_slew_idx() * grid.slew_step_ns)
                << ", output_slew_ns=" << (entry.get_output_slew_idx() * grid.slew_step_ns)
                << ", driven_cap_pf=" << (entry.get_driven_cap_idx() * grid.cap_step_pf)
                << ", load_cap_pf=" << (entry.get_load_cap_idx() * grid.cap_step_pf) << ", delay_ns=" << entry.get_delay();
  output_stream << ", power_w=" << std::scientific << std::setprecision(6) << entry.get_power() << "}";
  return output_stream.str();
}

template <class CharT, class FormatFn>
inline auto AppendExamples(std::ostringstream& output_stream, const std::string& prefix, std::vector<CharT> entries,
                           const FormatFn& format_fn) -> void
{
  SortCharsForReport(entries);
  for (std::size_t index = 0; index < std::min(kReportExampleLimit, entries.size()); ++index) {
    output_stream << prefix << format_fn(entries.at(index)) << "\n";
  }
}

inline auto SelectBestHTreeChar(const std::vector<icts::HTreeTopologyChar>& entries) -> std::optional<icts::HTreeTopologyChar>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  auto best = entries.front();
  for (const auto& entry : entries) {
    const bool better = (entry.get_load_cap_idx() > best.get_load_cap_idx())
                        || (entry.get_load_cap_idx() == best.get_load_cap_idx() && entry.get_delay() < best.get_delay())
                        || (entry.get_load_cap_idx() == best.get_load_cap_idx() && entry.get_delay() == best.get_delay()
                            && entry.get_output_slew_idx() < best.get_output_slew_idx())
                        || (entry.get_load_cap_idx() == best.get_load_cap_idx() && entry.get_delay() == best.get_delay()
                            && entry.get_output_slew_idx() == best.get_output_slew_idx() && entry.get_power() < best.get_power());
    if (better) {
      best = entry;
    }
  }
  return best;
}

}  // namespace icts_test::characterization::realtech
