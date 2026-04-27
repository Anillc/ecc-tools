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
#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"

namespace icts {

auto CharBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void
{
  _temp_inst_names.clear();
  _temp_net_names.clear();

  const std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  const auto& source_buf = _sorted_buffers.back();
  const auto& sink_buf = _sorted_buffers.front();

  _source_inst_name = id_prefix + "source";
  STA_ADAPTER_INST.createCharInstance(source_buf.cell_master, _source_inst_name);
  _source_in_pin = _source_inst_name + "/" + source_buf.input_pin;
  _source_out_pin = _source_inst_name + "/" + source_buf.output_pin;

  _sink_inst_name = id_prefix + "sink";
  STA_ADAPTER_INST.createCharInstance(sink_buf.cell_master, _sink_inst_name);
  _sink_in_pin = _sink_inst_name + "/" + sink_buf.input_pin;

  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    STA_ADAPTER_INST.createCharInstance(buf_masters.at(i), inst_name);
    _temp_inst_names.push_back(inst_name);
  }

  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    STA_ADAPTER_INST.createCharNet(net_name);
    _temp_net_names.push_back(net_name);
  }

  STA_ADAPTER_INST.attachCharPin(_source_inst_name, source_buf.output_pin, _temp_net_names.front());

  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    const CharBufferInfo* buf_info = findBufferInfo(buf_masters.at(bi));
    if (buf_info == nullptr) {
      LOG_FATAL << "Buffer info not found for: " << buf_masters.at(bi);
      return;
    }
    const auto& buffer_info = *buf_info;

    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.input_pin, _temp_net_names.at(bi));
    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.output_pin, _temp_net_names.at(bi + 1));
  }

  STA_ADAPTER_INST.attachCharPin(_sink_inst_name, sink_buf.input_pin, _temp_net_names.back());

  for (const auto& net_name : _temp_net_names) {
    STA_ADAPTER_INST.buildCharNetGraph(net_name);
  }

  _char_clock_name = id_prefix + "clk";

  ++_char_circuit_id;
}

auto CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf) -> void
{
  for (size_t i = 0; i < _temp_net_names.size(); ++i) {
    const double seg_len_um = topo.wire_segments_um.at(i);
    const double wire_res = STA_ADAPTER_INST.queryWireResistance(_routing_layer, seg_len_um, _wire_width);
    const double wire_cap = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);

    const double seg_load = (i == _temp_net_names.size() - 1) ? load_pf : 0.0;
    STA_ADAPTER_INST.buildCharRcTree(_temp_net_names.at(i), wire_res, wire_cap, seg_load);
  }
}

auto CharBuilder::destroyCharCircuit() -> void
{
  STA_ADAPTER_INST.resetCharContext();
  _sink_inst_name.clear();
  _source_inst_name.clear();
  _temp_net_names.clear();
  _temp_inst_names.clear();
  _source_in_pin.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
}

}  // namespace icts
