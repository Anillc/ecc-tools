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

#include "htree/CharacterizationLibrary.hh"

#include <glog/logging.h>

#include <ostream>

#include "CharBuilder.hh"
#include "Log.hh"
#include "config/Config.hh"

namespace icts {

auto CharacterizationLibrary::makeRequestKey(const CharBuilder::InitOptions& options) -> RequestKey
{
  return RequestKey{
      .wirelength_unit_um = options.wirelength_unit_um,
      .wirelength_iterations = options.wirelength_iterations,
      .wirelength_indices = options.wirelength_indices,
      .max_slew_ns = options.max_slew_ns,
      .max_cap_pf = options.max_cap_pf,
      .buffer_types = options.buffer_types,
      .char_buf_redundancy_pct = options.char_buf_redundancy_pct,
      .slew_steps = options.slew_steps,
      .cap_steps = options.cap_steps,
      .routing_layer = options.routing_layer,
      .wire_width = options.wire_width,
  };
}

auto CharacterizationLibrary::ensure(const CharBuilder::InitOptions& options) -> EnsureResult
{
  const auto request_key = makeRequestKey(options);
  if (_ready && request_key == _request_key && !_char_builder.get_segment_chars().empty()) {
    return EnsureResult{.success = true, .reused = true, .failure_reason = ""};
  }

  _char_builder.init(options);
  _char_builder.build();
  if (_char_builder.get_segment_chars().empty() || _char_builder.get_wirelength_unit_um() <= 0.0) {
    LOG_WARNING << "CharacterizationLibrary: characterization did not produce reusable segment chars.";
    _ready = false;
    _request_key = {};
    return EnsureResult{.success = false, .reused = false, .failure_reason = "no_reusable_segment_chars"};
  }

  _request_key = request_key;
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
  options.char_buf_redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();

  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  options.routing_layer = routing_layers.empty() ? 1 : static_cast<int>(routing_layers.front());
  if (CONFIG_INST.get_wire_width() > 0.0) {
    options.wire_width = CONFIG_INST.get_wire_width();
  }
  return options;
}

}  // namespace icts
