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
 * @file STAAdapterCharTiming.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter characterization timing sample implementation.
 */

#include <glog/logging.h>

#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Vector.hh"
#include "api/TimingEngine.hh"
#include "liberty/Lib.hh"
#include "netlist/DesignObject.hh"
#include "netlist/Instance.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sta/StaVertex.hh"

namespace icts {

auto STAAdapter::prepareCharTimingContext(const std::string& input_pin_full_name, const std::string& output_pin_full_name,
                                          const std::string& sink_pin_full_name) -> void
{
  auto& adapter = getInst();
  adapter.resetCharTimingState();

  auto& runtime = adapter._char_timing_state;
  auto* netlist = sta_adapter_internal::GetStaEngine()->get_netlist();
  auto source_input_match = netlist->findPin(input_pin_full_name.c_str(), false, false);
  auto source_output_match = netlist->findPin(output_pin_full_name.c_str(), false, false);
  runtime.source_input_pin = source_input_match.empty() ? nullptr : dynamic_cast<ista::Pin*>(source_input_match.front());
  runtime.source_output_pin = source_output_match.empty() ? nullptr : dynamic_cast<ista::Pin*>(source_output_match.front());
  runtime.source_input_vertex = sta_adapter_internal::FindStaVertex(input_pin_full_name);
  runtime.source_output_vertex = sta_adapter_internal::FindStaVertex(output_pin_full_name);
  runtime.sink_vertex = sta_adapter_internal::FindStaVertex(sink_pin_full_name);

  LOG_FATAL_IF(runtime.source_input_pin == nullptr) << "Cannot find source input pin: " << input_pin_full_name;
  LOG_FATAL_IF(runtime.source_output_pin == nullptr) << "Cannot find source output pin: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.source_input_vertex == nullptr) << "Cannot find source input vertex: " << input_pin_full_name;
  LOG_FATAL_IF(runtime.source_output_vertex == nullptr) << "Cannot find source output vertex: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.sink_vertex == nullptr) << "Cannot find sink input vertex: " << sink_pin_full_name;

  runtime.source_inst = runtime.source_output_pin->get_own_instance();
  LOG_FATAL_IF(runtime.source_inst == nullptr) << "Source output pin has no owning instance: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.source_input_pin->get_own_instance() != runtime.source_inst)
      << "Source input/output pins do not belong to the same instance: " << input_pin_full_name << " / " << output_pin_full_name;

  runtime.source_lib_cell = runtime.source_inst->get_inst_cell();
  LOG_FATAL_IF(runtime.source_lib_cell == nullptr) << "Source instance has no liberty cell: " << runtime.source_inst->get_name();

  auto timing_arc_set = sta_adapter_internal::FindBufferArcSet(runtime.source_lib_cell);
  runtime.source_arc_set = timing_arc_set.value_or(nullptr);
  LOG_FATAL_IF(runtime.source_arc_set == nullptr || runtime.source_arc_set->get_arcs().empty())
      << "Cannot resolve buffer timing arc for source instance " << runtime.source_inst->get_name();

  runtime.source_lib_arc = runtime.source_arc_set->front();
  LOG_FATAL_IF(runtime.source_lib_arc == nullptr) << "Source buffer liberty arc is null for instance " << runtime.source_inst->get_name();

  runtime.is_ready = true;
}

auto STAAdapter::prepareCharTimingSample() -> void
{
  sta_adapter_internal::GetStaEngine()->prepareCharTiming();
}

auto STAAdapter::setCharBufferInputSlew(double slew_ns) -> void
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before slew injection.";
  runtime.source_input_vertex->resetColor();
  runtime.source_input_vertex->resetLevel();
  runtime.source_input_vertex->reset_is_slew_prop();
  runtime.source_input_vertex->reset_is_delay_prop();
  runtime.source_input_vertex->reset_is_fwd();
  runtime.source_input_vertex->reset_is_bwd();
  runtime.source_input_vertex->resetSlewBucket();
  runtime.source_input_vertex->resetClockBucket();
  runtime.source_input_vertex->resetPathDelayBucket();
  runtime.source_output_vertex->resetSlewBucket();
  sta_adapter_internal::ApplyCharBufferInputSlew(runtime.source_input_vertex, runtime.source_output_pin, runtime.source_output_vertex,
                                                 runtime.source_inst, runtime.source_lib_cell, runtime.source_arc_set,
                                                 runtime.source_lib_arc, slew_ns);
}

auto STAAdapter::setCharBufferInputSlewIncremental(double slew_ns) -> void
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before incremental slew injection.";
  sta_adapter_internal::GetStaEngine()->prepareCharTiming();
  setCharBufferInputSlew(slew_ns);
}

auto STAAdapter::updateCharTimingSample() -> void
{
  sta_adapter_internal::GetStaEngine()->updateCharTiming();
}

auto STAAdapter::updateCharTimingIncrementalSample() -> void
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before incremental propagation.";
  sta_adapter_internal::GetStaEngine()->updateCharTiming();
}

}  // namespace icts
