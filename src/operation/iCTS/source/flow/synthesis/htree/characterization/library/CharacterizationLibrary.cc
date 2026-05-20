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
 * @file CharacterizationLibrary.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Reusable characterization cache shared by CTS synthesis stages.
 */

#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"

#include <glog/logging.h>

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "characterization/Characterization.hh"
#include "config/Config.hh"

namespace icts {
namespace {

auto BuildRuntimeCharacterizationBufferCells(const std::vector<std::string>& buffer_types) -> std::vector<CharacterizationBufferCell>
{
  std::vector<CharacterizationBufferCell> buffer_cells;
  buffer_cells.reserve(buffer_types.size());
  for (const auto& cell_master : buffer_types) {
    auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    buffer_cells.push_back(CharacterizationBufferCell{
        .cell_master = cell_master,
        .max_cap_pf = 0.0,
        .input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(cell_master),
        .input_slew_limit_ns = STA_ADAPTER_INST.queryCellInPinSlewLimit(cell_master),
        .input_slew_table_axis_max_ns = STA_ADAPTER_INST.queryCellInPinSlewTableAxisMax(cell_master),
        .output_cap_limit_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master),
        .output_cap_table_axis_max_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master),
        .cell_height_um = STA_ADAPTER_INST.queryCellHeightUm(cell_master),
        .input_pin = std::move(input_pin),
        .output_pin = std::move(output_pin),
    });
  }
  return buffer_cells;
}

}  // namespace

auto CharacterizationLibrary::makeCharacterizationCacheKey(const CharBuilder::InitOptions& options) -> CharacterizationCacheKey
{
  return CharacterizationCacheKey{
      .wirelength_unit_um = options.wirelength_unit_um,
      .wirelength_iterations = options.wirelength_iterations,
      .wirelength_indices = options.wirelength_indices,
      .max_slew_ns = options.max_slew_ns,
      .max_cap_pf = options.max_cap_pf,
      .buffer_types = options.buffer_types,
      .characterization_buffer_cells = options.characterization_buffer_cells,
      .char_buf_redundancy_pct = options.char_buf_redundancy_pct,
      .slew_steps = options.slew_steps,
      .cap_steps = options.cap_steps,
      .routing_layer = options.routing_layer,
      .wire_width = options.wire_width,
      .clock_route_segment_rc = options.clock_route_segment_rc,
  };
}

auto CharacterizationLibrary::ensure(const CharBuilder::InitOptions& options) -> EnsureResult
{
  const auto characterization_cache_key = makeCharacterizationCacheKey(options);
  if (_ready && characterization_cache_key == _characterization_cache_key && !_char_builder.get_segment_chars().empty()) {
    return EnsureResult{.success = true, .reused = true, .failure_reason = ""};
  }

  _char_builder.init(options);
  _char_builder.build();
  if (_char_builder.get_segment_chars().empty() || _char_builder.get_wirelength_unit_um() <= 0.0) {
    LOG_WARNING << "CharacterizationLibrary: characterization did not produce reusable segment chars.";
    _ready = false;
    _characterization_cache_key = {};
    return EnsureResult{.success = false, .reused = false, .failure_reason = "no_reusable_segment_chars"};
  }

  _characterization_cache_key = characterization_cache_key;
  _ready = true;
  return EnsureResult{.success = true, .reused = false, .failure_reason = ""};
}

auto CharacterizationLibrary::buildRuntimeOptions() -> CharBuilder::InitOptions
{
  CharBuilder::InitOptions options;
  if (CONFIG_INST.has_max_buf_tran() && CONFIG_INST.get_max_buf_tran() > 0.0) {
    options.max_slew_ns = CONFIG_INST.get_max_buf_tran();
  }
  if (CONFIG_INST.has_max_cap() && CONFIG_INST.get_max_cap() > 0.0) {
    options.max_cap_pf = CONFIG_INST.get_max_cap();
  }
  if (CONFIG_INST.get_wirelength_unit_um() > 0.0) {
    options.wirelength_unit_um = CONFIG_INST.get_wirelength_unit_um();
  }
  options.wirelength_iterations = CONFIG_INST.get_wirelength_iterations();
  options.slew_steps = CONFIG_INST.get_slew_steps();
  options.cap_steps = CONFIG_INST.get_cap_steps();
  options.buffer_types = CONFIG_INST.get_buffer_types();
  options.characterization_buffer_cells = BuildRuntimeCharacterizationBufferCells(options.buffer_types);
  options.char_buf_redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();

  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  LOG_FATAL_IF(routing_layers.empty() || routing_layers.front() == 0U)
      << "CharacterizationLibrary: routing layer must be configured before characterization.";
  options.routing_layer = static_cast<int>(routing_layers.front());
  if (CONFIG_INST.get_wire_width() > 0.0) {
    options.wire_width = CONFIG_INST.get_wire_width();
  }
  options.clock_route_segment_rc = STA_ADAPTER_INST.queryConfiguredClockRouteSegmentRc();
  return options;
}

}  // namespace icts
