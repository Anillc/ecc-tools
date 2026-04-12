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
 * @file Config.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief Configuration for CTS
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace icts {

#define CONFIG_INST (icts::Config::getInst())

class Config
{
 public:
  static auto getInst() -> Config&
  {
    static Config inst;
    return inst;
  }

  // Delete copy and move constructors
  Config(const Config& rhs) = delete;
  Config(Config&& rhs) = delete;
  auto operator=(const Config& rhs) -> Config& = delete;
  auto operator=(Config&& rhs) -> Config& = delete;

  // Initialize from config file
  auto init(const std::string& config_file) -> void { parse(config_file); }

  // Reset to default values
  auto reset() -> void
  {
    _skew_bound = 0.04;
    _max_buf_tran = 1.5;
    _max_sink_tran = 1.5;
    _max_cap = 1.5;
    _max_fanout = 32;
    _max_length = 300;
    _routing_layers.clear();
    _buffer_types.clear();
    _slew_steps = 20;
    _cap_steps = 20;
    _length_steps = 20;
    _char_buf_redundancy_pct = 0.0;
    _work_dir = "./result/cts";
    _output_def_path = "./result/cts/output";
    _log_file = "./result/cts/cts.log";
    _gds_file = "./result/cts/output/cts.gds";
    _use_netlist = false;
    _net_list.clear();
  }

  // algorithm
  auto get_skew_bound() const -> double { return _skew_bound; }
  auto get_max_buf_tran() const -> double { return _max_buf_tran; }
  auto get_max_sink_tran() const -> double { return _max_sink_tran; }
  auto get_max_cap() const -> double { return _max_cap; }
  auto get_max_length() const -> double { return _max_length; }
  auto get_slew_steps() const -> unsigned { return _slew_steps; }
  auto get_cap_steps() const -> unsigned { return _cap_steps; }
  auto get_length_steps() const -> unsigned { return _length_steps; }
  auto get_max_pattern_nodes() const -> unsigned { return _max_pattern_nodes; }
  auto get_wire_width() const -> double { return _wire_width; }
  auto get_max_fanout() const -> unsigned { return _max_fanout; }
  auto get_routing_layers() const -> const std::vector<unsigned>& { return _routing_layers; }
  auto get_buffer_types() const -> const std::vector<std::string>& { return _buffer_types; }
  auto get_char_buf_redundancy_pct() const -> double { return _char_buf_redundancy_pct; }

  // file
  auto get_work_dir() const -> const std::string& { return _work_dir; }
  auto get_output_def_path() const -> const std::string& { return _output_def_path; }
  auto get_log_file() const -> const std::string& { return _log_file; }
  auto get_gds_file() const -> const std::string& { return _gds_file; }
  auto is_use_netlist() const -> bool { return _use_netlist; }
  auto get_net_list() const -> const std::vector<std::pair<std::string, std::string>> { return _net_list; }

  // algorithm
  auto set_skew_bound(double skew_bound) -> void { _skew_bound = skew_bound; }
  auto set_max_buf_tran(double max_buf_tran) -> void { _max_buf_tran = max_buf_tran; }
  auto set_max_sink_tran(double max_sink_tran) -> void { _max_sink_tran = max_sink_tran; }
  auto set_max_cap(double max_cap) -> void { _max_cap = max_cap; }
  auto set_max_length(double max_length) -> void { _max_length = max_length; }
  auto set_slew_steps(unsigned steps) -> void { _slew_steps = steps; }
  auto set_cap_steps(unsigned steps) -> void { _cap_steps = steps; }
  auto set_length_steps(unsigned steps) -> void { _length_steps = steps; }
  auto set_max_pattern_nodes(unsigned nodes) -> void { _max_pattern_nodes = nodes; }
  auto set_wire_width(double wire_width) -> void { _wire_width = wire_width; }
  auto set_max_fanout(unsigned max_fanout) -> void { _max_fanout = max_fanout; }
  auto set_routing_layers(const std::vector<unsigned>& routing_layers) -> void { _routing_layers = routing_layers; }
  auto set_buffer_types(const std::vector<std::string>& types) -> void { _buffer_types = types; }
  auto set_char_buf_redundancy_pct(double pct) -> void { _char_buf_redundancy_pct = pct; }

  // file
  auto set_work_dir(const std::string& work_dir) -> void { _work_dir = work_dir; }
  auto set_output_def_path(const std::string& output_def_path) -> void { _output_def_path = output_def_path; }
  auto set_log_file(const std::string& file) -> void { _log_file = file; }
  auto set_gds_file(const std::string& file) -> void { _gds_file = file; }
  auto set_use_netlist(bool use_netlist) -> void { _use_netlist = use_netlist; }
  auto set_net_list(const std::vector<std::pair<std::string, std::string>>& net_list) -> void { _net_list = net_list; }

  // parse from json file
  auto parse(const std::string& json_file) -> void;

 private:
  Config() = default;
  ~Config() = default;

  // algorithm
  double _skew_bound = 0.0;
  double _max_buf_tran = 0.0;
  double _max_sink_tran = 0.0;
  double _max_cap = 0.0;
  double _max_length = 0.0;
  unsigned _slew_steps = 20;
  unsigned _cap_steps = 20;
  unsigned _length_steps = 20;
  unsigned _max_pattern_nodes = 8;
  double _wire_width = 0.0;
  unsigned _max_fanout = 32;
  std::vector<unsigned> _routing_layers;
  std::vector<std::string> _buffer_types;
  double _char_buf_redundancy_pct = 0.0;

  // file
  std::string _work_dir = "./result/cts";
  std::string _output_def_path = "./result/cts/output";
  std::string _log_file = "./result/cts/cts.log";
  std::string _gds_file = "./result/cts/output/cts.gds";

  bool _use_netlist = false;
  std::vector<std::pair<std::string, std::string>> _net_list;
};

}  // namespace icts
