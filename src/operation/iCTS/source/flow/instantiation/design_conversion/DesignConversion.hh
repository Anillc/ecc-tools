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
 * @file DesignConversion.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS design conversion and materialization helper.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace icts {

class Clock;
class Inst;
class Net;
class Pin;
enum class SinkDomainKind;

class DesignConversion
{
 public:
  DesignConversion() = delete;

  static auto readClockData() -> void;
  static auto partitionClockSinks(const std::vector<Pin*>& sinks, std::vector<Pin*>& macro_sinks, std::vector<Pin*>& regular_sinks) -> void;
  static auto makeSinkDomainPrefix(const Clock& clock, std::size_t clock_index, SinkDomainKind sink_domain) -> std::string;
  static auto addRootBufferForSinkDomain(Clock& clock, const std::string& domain_prefix, const std::vector<Pin*>& sinks, Inst*& root_buffer,
                                         Pin*& root_input, Pin*& root_output) -> bool;
  static auto addRootBufferForSinkDomain(Clock& clock, const std::string& domain_prefix, const std::string& cell_master,
                                         const std::string& input_pin_name, const std::string& output_pin_name,
                                         const std::vector<Pin*>& sinks, Inst*& root_buffer, Pin*& root_input, Pin*& root_output) -> bool;
  static auto reconnectNet(Net& net, Pin* driver, const std::vector<Pin*>& loads) -> void;
  static auto connectSinkDomainDownstreamNet(Clock& clock, const std::string& domain_prefix, Pin* root_output,
                                             const std::vector<Pin*>& sinks) -> Net*;
  static auto restoreClockSourceNetToClockLoads(Clock& clock) -> void;
  static auto reuseClockSourceNetAsSourceToRootBuffers(Clock& clock, Pin* clock_source, const std::vector<Pin*>& root_buffer_inputs)
      -> Net*;
  static auto commitInsertedObjects(Clock& clock, std::vector<std::unique_ptr<Inst>>& inserted_insts,
                                    std::vector<std::unique_ptr<Pin>>& inserted_pins, std::vector<std::unique_ptr<Net>>& inserted_nets)
      -> bool;
};

}  // namespace icts
