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
 * @file CharBuilderCircuit.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Temporary characterization circuit and parasitic setup.
 */

#include <glog/logging.h>

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "CharBuilder.hh"
#include "FastStaTypes.hh"
#include "Log.hh"
#include "adapter/fast_sta/FastSta.hh"

namespace icts {

auto CharBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void
{
  _temp_inst_names.clear();
  _temp_net_names.clear();
  _fast_sta_char_context_id = kInvalidFastStaCharContextId;

  const std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  const auto& source_buf = _sorted_buffers.back();
  const auto& sink_buf = _sorted_buffers.front();

  _source_inst_name = id_prefix + "source";
  _source_in_pin = _source_inst_name + "/" + source_buf.input_pin;
  _source_out_pin = _source_inst_name + "/" + source_buf.output_pin;
  _timing_observation_pin.clear();

  _sink_inst_name = id_prefix + "sink";
  _sink_in_pin = _sink_inst_name + "/" + sink_buf.input_pin;

  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    _temp_inst_names.push_back(inst_name);
  }

  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    _temp_net_names.push_back(net_name);
  }

  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    const CharBufferInfo* buf_info = findBufferInfo(buf_masters.at(bi));
    if (buf_info == nullptr) {
      LOG_FATAL << "Buffer info not found for: " << buf_masters.at(bi);
      return;
    }
    const auto& buffer_info = *buf_info;

    if (bi + 1U == buf_masters.size()) {
      _timing_observation_pin = _temp_inst_names.at(bi) + "/" + buffer_info.output_pin;
    }
  }

  if (_timing_observation_pin.empty()) {
    _timing_observation_pin = _sink_in_pin;
  }

  _char_clock_name = id_prefix + "clk";
  _fast_sta_char_context_id = FastSTA::buildCharContext(FastStaCharTopologySpec{
      .source_cell_master = source_buf.cell_master,
      .sink_cell_master = sink_buf.cell_master,
      .buffer_cell_masters = buf_masters,
      .wire_segments_um = topo.wire_segments_um,
      .routing_layer = _routing_layer,
      .wire_width_um = _wire_width,
      .clock_period_ns = 10.0,
  });

  ++_char_circuit_id;
}

auto CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf) const -> void
{
  (void) topo;
  LOG_FATAL_IF(_fast_sta_char_context_id == kInvalidFastStaCharContextId)
      << "Fast STA characterization context is not prepared before parasitic load update.";
  (void) FastSTA::setCharLoad(_fast_sta_char_context_id, load_pf);
}

auto CharBuilder::destroyCharCircuit() -> void
{
  if (_fast_sta_char_context_id != kInvalidFastStaCharContextId) {
    (void) FastSTA::eraseCharContext(_fast_sta_char_context_id);
    _fast_sta_char_context_id = kInvalidFastStaCharContextId;
  }
  _sink_inst_name.clear();
  _source_inst_name.clear();
  _temp_net_names.clear();
  _temp_inst_names.clear();
  _source_in_pin.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
  _timing_observation_pin.clear();
}

}  // namespace icts
