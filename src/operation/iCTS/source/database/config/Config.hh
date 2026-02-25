// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace icts {

#define CTSConfigInst (icts::Config::getInst())

class Config
{
 public:
  static Config& getInst()
  {
    static Config instance;
    return instance;
  }

  // Delete copy and move constructors
  Config(const Config& rhs) = delete;
  Config(Config&& rhs) = delete;
  Config& operator=(const Config& rhs) = delete;
  Config& operator=(Config&& rhs) = delete;

  // Initialize from config file
  void init(const std::string& config_file) { parse(config_file); }

  // Reset to default values
  void reset()
  {
    _skew_bound = 0.04;
    _max_buf_tran = 1.5;
    _max_sink_tran = 1.5;
    _max_cap = 1.5;
    _max_fanout = 32;
    _max_length = 300;
    _routing_layers.clear();
    _buffer_types.clear();
    _work_dir = "./result/cts";
    _output_def_path = "./result/cts/output";
    _log_file = "./result/cts/cts.log";
    _gds_file = "./result/cts/output/cts.gds";
    _use_netlist = false;
    _net_list.clear();
  }

  // algorithm
  double get_skew_bound() const { return _skew_bound; }
  double get_max_buf_tran() const { return _max_buf_tran; }
  double get_max_sink_tran() const { return _max_sink_tran; }
  double get_max_cap() const { return _max_cap; }
  double get_max_length() const { return _max_length; }
  double get_slew_unit() const { return _slew_unit; }
  double get_cap_unit() const { return _cap_unit; }
  double get_length_unit() const { return _length_unit; }
  unsigned get_max_pattern_nodes() const { return _max_pattern_nodes; }
  double get_wire_width() const { return _wire_width; }
  unsigned get_max_fanout() const { return _max_fanout; }
  const std::vector<unsigned>& get_routing_layers() const { return _routing_layers; }
  const std::vector<std::string>& get_buffer_types() const { return _buffer_types; }

  // file
  const std::string& get_work_dir() const { return _work_dir; }
  const std::string& get_output_def_path() const { return _output_def_path; }
  const std::string& get_log_file() const { return _log_file; }
  const std::string& get_gds_file() const { return _gds_file; }
  bool is_use_netlist() const { return _use_netlist; }
  const std::vector<std::pair<std::string, std::string>> get_clock_netlist() const { return _net_list; }

  // algorithm
  void set_skew_bound(double skew_bound) { _skew_bound = skew_bound; }
  void set_max_buf_tran(double max_buf_tran) { _max_buf_tran = max_buf_tran; }
  void set_max_sink_tran(double max_sink_tran) { _max_sink_tran = max_sink_tran; }
  void set_max_cap(double max_cap) { _max_cap = max_cap; }
  void set_max_length(double max_length) { _max_length = max_length; }
  void set_slew_unit(double slew_unit) { _slew_unit = slew_unit; }
  void set_cap_unit(double cap_unit) { _cap_unit = cap_unit; }
  void set_length_unit(double length_unit) { _length_unit = length_unit; }
  void set_max_pattern_nodes(unsigned nodes) { _max_pattern_nodes = nodes; }
  void set_wire_width(double wire_width) { _wire_width = wire_width; }
  void set_max_fanout(unsigned max_fanout) { _max_fanout = max_fanout; }
  void set_routing_layers(const std::vector<unsigned>& routing_layers) { _routing_layers = routing_layers; }
  void set_buffer_types(const std::vector<std::string>& types) { _buffer_types = types; }

  // file
  void set_work_dir(const std::string& work_dir) { _work_dir = work_dir; }
  void set_output_def_path(const std::string& output_def_path) { _output_def_path = output_def_path; }
  void set_log_file(const std::string& file) { _log_file = file; }
  void set_gds_file(const std::string& file) { _gds_file = file; }
  void set_use_netlist(bool use_netlist) { _use_netlist = use_netlist; }
  void set_netlist(const std::vector<std::pair<std::string, std::string>>& net_list) { _net_list = net_list; }

  // parse from json file
  void parse(const std::string& json_file);

 private:
  Config() = default;
  ~Config() = default;

  // algorithm
  double _skew_bound = 0.0;
  double _max_buf_tran = 0.0;
  double _max_sink_tran = 0.0;
  double _max_cap = 0.0;
  double _max_length = 0.0;
  double _slew_unit = 0.05;
  double _cap_unit = 0.05;
  double _length_unit = 25;
  unsigned _max_pattern_nodes = 8;
  double _wire_width = 0.0;
  unsigned _max_fanout = 32;
  std::vector<unsigned> _routing_layers;
  std::vector<std::string> _buffer_types;

  // file
  std::string _work_dir = "./result/cts";
  std::string _output_def_path = "./result/cts/output";
  std::string _log_file = "./result/cts/cts.log";
  std::string _gds_file = "./result/cts/output/cts.gds";

  bool _use_netlist = false;
  std::vector<std::pair<std::string, std::string>> _net_list;
};

}  // namespace icts
