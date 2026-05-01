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
 * @file SourceTrunkSegment.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Source-to-sink segment synthesis using reusable characterization frontiers.
 */

#include "synthesis/topology/trunk/SourceTrunkSegment.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "Inst.hh"
#include "Log.hh"
#include "LogFormat.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "geometry/Geometry.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/htree/characterization/wirelength/WirelengthGrid.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/embedding/BufferPortTable.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"

namespace icts {
namespace {

auto FormatLogValue(const std::string& value) -> std::string
{
  return value.empty() ? "n/a" : value;
}

auto MakeObjectName(const std::string& prefix, const std::string& suffix) -> std::string
{
  return prefix.empty() ? "cts_" + suffix : prefix + "_" + suffix;
}

auto ConnectNet(Net& net, Pin* driver, const std::vector<Pin*>& loads) -> void
{
  auto* old_driver = net.get_driver();
  if (old_driver != nullptr && old_driver != driver && old_driver->get_net() == &net) {
    old_driver->set_net(nullptr);
  }

  for (auto* old_load : net.get_loads()) {
    if (old_load != nullptr && old_load->get_net() == &net) {
      old_load->set_net(nullptr);
    }
  }

  net.set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(&net);
  }

  net.set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net.add_load(load);
    load->set_net(&net);
  }
}

auto ConnectOwnedNet(Net& net, Pin* driver, const std::vector<Pin*>& loads) -> void
{
  net.set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(&net);
  }

  net.set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net.add_load(load);
    load->set_net(&net);
  }
}

auto CreateBufferInstance(SourceTrunkSegment::BuildResult& result, const std::string& inst_name, const std::string& cell_master,
                          const Point<int>& location, const std::string& input_pin_name, const std::string& output_pin_name)
    -> std::pair<Pin*, Pin*>
{
  auto inst = std::make_unique<Inst>(inst_name, cell_master, InstType::kBuffer, location);
  auto* inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(input_pin_name, PinType::kIn, location, inst_ptr, nullptr, false);
  auto* input_pin_ptr = input_pin.get();
  result.inserted_pins.push_back(std::move(input_pin));

  auto output_pin = std::make_unique<Pin>(output_pin_name, PinType::kOut, location, inst_ptr, nullptr, false);
  auto* output_pin_ptr = output_pin.get();
  result.inserted_pins.push_back(std::move(output_pin));

  inst_ptr->add_pin(input_pin_ptr);
  inst_ptr->insertDriverPin(output_pin_ptr);
  result.inserted_insts.push_back(std::move(inst));

  return {input_pin_ptr, output_pin_ptr};
}

auto RecordInsertedInstLevel(SourceTrunkSegment::BuildResult& result, Inst* inst, int topology_level, std::size_t index_in_level) -> void
{
  if (inst == nullptr) {
    return;
  }
  result.inserted_inst_levels.push_back(HTree::InsertedInstLevel{
      .inst = inst,
      .topology_level = topology_level,
      .index_in_level = index_in_level,
  });
}

auto RecordInsertedNetLevel(SourceTrunkSegment::BuildResult& result, Net* net, int topology_level, std::size_t index_in_level) -> void
{
  if (net == nullptr) {
    return;
  }
  result.inserted_net_levels.push_back(HTree::InsertedNetLevel{
      .net = net,
      .topology_level = topology_level,
      .index_in_level = index_in_level,
  });
}

auto CreateNet(SourceTrunkSegment::BuildResult& result, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads,
               int topology_level, std::size_t index_in_level) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();
  ConnectOwnedNet(*net_ptr, driver, loads);
  RecordInsertedNetLevel(result, net_ptr, topology_level, index_in_level);
  result.inserted_nets.push_back(std::move(net));
  return net_ptr;
}

auto DelayPowerDominates(const SegmentChar& lhs, const SegmentChar& rhs) -> bool
{
  const bool not_worse = lhs.get_delay() <= rhs.get_delay() && lhs.get_power() <= rhs.get_power();
  const bool strictly_better = lhs.get_delay() < rhs.get_delay() || lhs.get_power() < rhs.get_power();
  return not_worse && strictly_better;
}

auto PreferSegmentEntry(const SegmentChar& lhs, const SegmentChar& rhs) -> bool
{
  if (lhs.get_power() != rhs.get_power()) {
    return lhs.get_power() < rhs.get_power();
  }
  if (lhs.get_delay() != rhs.get_delay()) {
    return lhs.get_delay() < rhs.get_delay();
  }
  if (lhs.get_driven_cap_idx() != rhs.get_driven_cap_idx()) {
    return lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx();
  }
  if (lhs.get_output_slew_idx() != rhs.get_output_slew_idx()) {
    return lhs.get_output_slew_idx() < rhs.get_output_slew_idx();
  }
  if (lhs.get_load_cap_idx() != rhs.get_load_cap_idx()) {
    return lhs.get_load_cap_idx() < rhs.get_load_cap_idx();
  }
  if (lhs.get_input_slew_idx() != rhs.get_input_slew_idx()) {
    return lhs.get_input_slew_idx() > rhs.get_input_slew_idx();
  }
  return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
}

auto BuildDelayPowerParetoFront(const std::vector<SegmentChar>& entries) -> std::vector<SegmentChar>
{
  std::vector<SegmentChar> pareto_front;
  pareto_front.reserve(entries.size());
  for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
    bool dominated = false;
    for (std::size_t other_index = 0; other_index < entries.size(); ++other_index) {
      if (entry_index == other_index) {
        continue;
      }
      if (DelayPowerDominates(entries.at(other_index), entries.at(entry_index))) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_front.push_back(entries.at(entry_index));
    }
  }

  std::ranges::sort(pareto_front, PreferSegmentEntry);
  return pareto_front;
}

auto SelectBestSegmentEntry(const std::vector<SegmentChar>& entries) -> std::optional<SegmentChar>
{
  if (entries.empty()) {
    return std::nullopt;
  }
  auto pareto_front = BuildDelayPowerParetoFront(entries);
  if (pareto_front.empty()) {
    return std::nullopt;
  }
  const std::size_t median_index = (pareto_front.size() - 1U) / 2U;
  return pareto_front.at(median_index);
}

auto FilterSegmentEntries(const std::vector<SegmentChar>& entries, unsigned required_load_cap_idx, unsigned source_drive_cap_idx,
                          const std::optional<unsigned>& min_input_slew_idx) -> std::vector<SegmentChar>
{
  std::vector<SegmentChar> filtered_entries;
  filtered_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    if (entry.get_load_cap_idx() < required_load_cap_idx) {
      continue;
    }
    if (entry.get_driven_cap_idx() > source_drive_cap_idx) {
      continue;
    }
    if (min_input_slew_idx.has_value() && entry.get_input_slew_idx() < *min_input_slew_idx) {
      continue;
    }
    filtered_entries.push_back(entry);
  }
  return filtered_entries;
}

auto BuildSourceTrunkSegmentObjects(SourceTrunkSegment::BuildResult& result, Net& source_net, Pin* source, Pin* sink,
                                    const BufferingPattern& pattern, const SourceTrunkSegment::BuildOptions& options) -> bool
{
  const auto& cell_masters = pattern.get_cell_masters();
  const auto& positions = pattern.get_buffer_positions();
  const std::size_t buffer_count = std::min(cell_masters.size(), positions.size());
  if (buffer_count == 0U) {
    ConnectNet(source_net, source, {sink});
    return true;
  }

  htree::BufferPortTable port_table;
  std::vector<std::pair<Pin*, Pin*>> segment_buffers;
  segment_buffers.reserve(buffer_count);
  for (std::size_t buffer_index = 0; buffer_index < buffer_count; ++buffer_index) {
    const auto* ports = port_table.get(cell_masters.at(buffer_index));
    if (ports == nullptr) {
      LOG_WARNING << "SourceTrunkSegment: unresolved ports for source-to-root buffer master " << cell_masters.at(buffer_index) << ".";
      result.failure_reason = "unresolved_buffer_ports";
      return false;
    }

    const auto location = htree::InterpolateManhattanPoint(source->get_location(), sink->get_location(), positions.at(buffer_index));
    auto created_buffer
        = CreateBufferInstance(result, MakeObjectName(options.object_name_prefix, "top_segment_buf_" + std::to_string(buffer_index)),
                               cell_masters.at(buffer_index), location, ports->first, ports->second);
    RecordInsertedInstLevel(result, created_buffer.first == nullptr ? nullptr : created_buffer.first->get_inst(),
                            static_cast<int>(buffer_index), buffer_index);
    segment_buffers.push_back(created_buffer);
  }

  ConnectNet(source_net, source, {segment_buffers.front().first});
  for (std::size_t buffer_index = 0; buffer_index + 1U < segment_buffers.size(); ++buffer_index) {
    CreateNet(result, MakeObjectName(options.object_name_prefix, "top_segment_net_" + std::to_string(buffer_index)),
              segment_buffers.at(buffer_index).second, {segment_buffers.at(buffer_index + 1U).first}, static_cast<int>(buffer_index),
              buffer_index);
  }
  CreateNet(result, MakeObjectName(options.object_name_prefix, "top_segment_net_" + std::to_string(segment_buffers.size() - 1U)),
            segment_buffers.back().second, {sink}, static_cast<int>(segment_buffers.size() - 1U), segment_buffers.size() - 1U);
  return true;
}

auto EmitSegmentSummary(const SourceTrunkSegment::BuildResult& result, const SourceTrunkSegment::BuildOptions& options) -> void
{
  schema::EmitKeyValueTable(
      "SourceTrunkSegment Build Overview",
      {
          {"clock_name", FormatLogValue(options.log_context.clock_name)},
          {"clock_net_name", FormatLogValue(options.log_context.clock_net_name)},
          {"sink_domain", FormatLogValue(options.log_context.sink_domain)},
          {"stage", FormatLogValue(options.log_context.stage)},
          {"object_name_prefix", FormatLogValue(options.object_name_prefix)},
          {"status", result.success ? "finished" : "failed"},
          {"length", logformat::FormatWithUnit(result.length_um, "um")},
          {"required_load_cap", logformat::FormatWithUnit(options.required_load_cap_pf, "pF")},
          {"source_drive_cap", logformat::FormatWithUnit(options.source_drive_cap_pf, "pF")},
          {"min_input_slew", options.min_input_slew_ns.has_value() ? logformat::FormatWithUnit(*options.min_input_slew_ns, "ns") : "none"},
          {"length_idx", std::to_string(result.length_idx)},
          {"required_load_cap_idx", std::to_string(result.required_load_cap_idx)},
          {"source_drive_cap_idx", std::to_string(result.source_drive_cap_idx)},
          {"min_input_slew_idx", result.min_input_slew_idx.has_value() ? std::to_string(*result.min_input_slew_idx) : "none"},
          {"strict_candidate_count", std::to_string(result.strict_candidate_count)},
          {"fallback_candidate_count", std::to_string(result.fallback_candidate_count)},
          {"used_boundary_fallback", logformat::FormatBool(result.used_boundary_fallback)},
          {"boundary_fallback_reason", result.boundary_fallback_reason.empty() ? "none" : result.boundary_fallback_reason},
          {"inserted_insts", std::to_string(result.inserted_insts.size())},
          {"inserted_nets", std::to_string(result.inserted_nets.size())},
      });
}

auto ConfigureCharOptions(const std::vector<double>& requested_lengths_um) -> CharBuilder::InitOptions
{
  auto char_options = CharacterizationLibrary::buildRuntimeOptions();
  const auto char_grid_plan = htree::ResolveCharacterizationGridPlan(requested_lengths_um);
  if (!char_grid_plan.adapted) {
    return char_options;
  }

  char_options.wirelength_unit_um = char_grid_plan.wirelength_unit_um;
  char_options.wirelength_iterations = char_grid_plan.wirelength_iterations;
  auto direct_indices = htree::ResolveDirectCharacterizationLengthIndices(requested_lengths_um, char_grid_plan);
  if (!direct_indices.empty()) {
    char_options.wirelength_indices = std::move(direct_indices);
  }
  return char_options;
}

}  // namespace

auto SourceTrunkSegment::build(Net& source_net, Pin* source, Pin* sink, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  if (source == nullptr || sink == nullptr) {
    result.failure_reason = "null_source_or_sink_pin";
    LOG_ERROR << "SourceTrunkSegment: source-to-root build failed because source or sink pin is null.";
    EmitSegmentSummary(result, options);
    return result;
  }

  const int distance_dbu = geometry::Manhattan(source->get_location(), sink->get_location());
  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  result.length_um = static_cast<double>(std::max(distance_dbu, 0)) / static_cast<double>(dbu_per_um);
  if (result.length_um <= 0.0) {
    ConnectNet(source_net, source, {sink});
    result.success = true;
    EmitSegmentSummary(result, options);
    return result;
  }

  if (options.required_load_cap_pf <= 0.0) {
    result.failure_reason = "unresolved_required_load_cap";
    LOG_ERROR << "SourceTrunkSegment: source-to-root segment requires a positive root-input load cap.";
    EmitSegmentSummary(result, options);
    return result;
  }
  if (options.source_drive_cap_pf <= 0.0) {
    result.failure_reason = "unresolved_source_drive_cap";
    LOG_ERROR << "SourceTrunkSegment: source-to-root segment requires a positive source drive cap.";
    EmitSegmentSummary(result, options);
    return result;
  }

  CharacterizationLibrary local_char_library;
  auto* char_library = options.characterization_library == nullptr ? &local_char_library : options.characterization_library;
  const std::vector<double> requested_lengths_um{result.length_um};
  if (!char_library->isReady()) {
    const auto ensure_result = char_library->ensure(ConfigureCharOptions(requested_lengths_um));
    if (!ensure_result.success) {
      result.failure_reason = ensure_result.failure_reason.empty() ? "characterization_library_failed" : ensure_result.failure_reason;
      EmitSegmentSummary(result, options);
      return result;
    }
  }
  const auto& char_builder = char_library->getCharBuilder();
  if (char_builder.get_segment_chars().empty() || char_builder.get_wirelength_unit_um() <= 0.0) {
    result.failure_reason = "no_usable_segment_chars";
    EmitSegmentSummary(result, options);
    return result;
  }

  result.length_idx = char_builder.get_length_lattice().coveringIndex(result.length_um);
  result.required_load_cap_idx = htree::CoveringBoundaryIndex(options.required_load_cap_pf, char_builder.get_cap_lattice()).value_or(0U);
  result.source_drive_cap_idx = htree::CoveringBoundaryIndex(options.source_drive_cap_pf, char_builder.get_cap_lattice()).value_or(0U);
  if (options.min_input_slew_ns.has_value()) {
    result.min_input_slew_idx = htree::CoveringBoundaryIndex(*options.min_input_slew_ns, char_builder.get_slew_lattice());
  }

  if (result.length_idx == 0U || result.required_load_cap_idx == 0U || result.source_drive_cap_idx == 0U) {
    result.failure_reason = "unresolved_segment_boundary_indices";
    EmitSegmentSummary(result, options);
    return result;
  }
  if (result.required_load_cap_idx > char_builder.get_cap_steps()) {
    result.failure_reason = "segment_hard_boundary_out_of_range";
    EmitSegmentSummary(result, options);
    return result;
  }

  htree::BufferPatternLibrary pattern_library;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    pattern_library.add(pattern);
  }
  auto entry_sets_by_length = htree::SynthesizeSegmentEntrySets(char_builder.get_segment_chars(), pattern_library, {result.length_idx});
  const auto entry_set_it = entry_sets_by_length.find(result.length_idx);
  if (entry_set_it == entry_sets_by_length.end() || entry_set_it->second.all_frontier_entries.empty()) {
    result.failure_reason = "missing_required_segment_frontier";
    EmitSegmentSummary(result, options);
    return result;
  }

  auto strict_entries = FilterSegmentEntries(entry_set_it->second.all_frontier_entries, result.required_load_cap_idx,
                                             result.source_drive_cap_idx, result.min_input_slew_idx);
  result.strict_candidate_count = strict_entries.size();
  result.best_char = SelectBestSegmentEntry(strict_entries);
  if (!result.best_char.has_value() && result.min_input_slew_idx.has_value()) {
    auto fallback_entries = FilterSegmentEntries(entry_set_it->second.all_frontier_entries, result.required_load_cap_idx,
                                                 result.source_drive_cap_idx, std::nullopt);
    result.fallback_candidate_count = fallback_entries.size();
    result.best_char = SelectBestSegmentEntry(fallback_entries);
    if (result.best_char.has_value()) {
      result.used_boundary_fallback = true;
      result.boundary_fallback_reason = "dropped_soft_input_slew_boundary";
    }
  }
  if (!result.best_char.has_value()) {
    result.failure_reason = "no_hard_boundary_legal_segment_candidate";
    EmitSegmentSummary(result, options);
    return result;
  }

  const auto* selected_pattern = pattern_library.find(result.best_char->get_pattern_id());
  if (selected_pattern == nullptr) {
    result.failure_reason = "missing_selected_segment_pattern";
    EmitSegmentSummary(result, options);
    return result;
  }
  result.success = BuildSourceTrunkSegmentObjects(result, source_net, source, sink, *selected_pattern, options);
  EmitSegmentSummary(result, options);
  return result;
}

}  // namespace icts
