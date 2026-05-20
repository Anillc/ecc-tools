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
#pragma once

#include <string>
#include <utility>

#include "Environment.hh"
#include "ProcessVariation.hh"
#include "Types.hh"

namespace ircx {

#define RCX_FLOW_INST (ircx::Flow::getInst())

class Flow final {
 public:
  // Meyer's singleton
  static Flow& getInst() {
    static Flow inst;  // C++11 thread-safe
    return inst;
  }

  // Disallow copy/move
  Flow(const Flow&) = delete;
  Flow& operator=(const Flow&) = delete;
  Flow(Flow&&) = delete;
  Flow& operator=(Flow&&) = delete;

  // Checks
  [[nodiscard]] unsigned checkShortOpen();

  // Topology
  [[nodiscard]] unsigned buildTopology();

  // Environment
  [[nodiscard]] unsigned buildEnvironment();

  // Process
  [[nodiscard]] unsigned buildProcessVariation();

  // Extraction
  [[nodiscard]] unsigned extractParasitics();
  [[nodiscard]] unsigned run();

  // Report
  [[nodiscard]] unsigned reportSpef(const Str& output_dir);

  // setters & getters
  void set_num_threads(unsigned value) { num_threads_ = value == 0 ? 1U : value; }
  void set_operating_temperature(F64 value) { operating_temperature_ = value; }
  void set_output_dir(Str value) { output_dir_ = value.empty() ? "." : std::move(value); }
  [[nodiscard]] unsigned num_threads() const { return num_threads_; }
  [[nodiscard]] F64 operating_temperature() const { return operating_temperature_; }
  [[nodiscard]] const Str& output_dir() const { return output_dir_; }

  void reset();

 private:
  Flow();
  ~Flow();

 private:
  // running settings
  unsigned num_threads_{};
  F64 operating_temperature_{};
  Str output_dir_{"."};

  Environment environment_;
  ProcessVariation process_variation_;
};

}  // namespace ircx
