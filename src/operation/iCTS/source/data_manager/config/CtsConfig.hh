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
 * @file CtsConfig.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <string>
#include <vector>

namespace icts {
/**
 * @brief RC Pattern definition
 *
 */
enum class RCPattern
{
  kSingle,  // single layer
  kHV,      // first horizontal then vertical
  kVH,      // first vertical then horizontal
};
/**
 * @brief Layer Pattern definition
 *
 */
enum class LayerPattern
{
  kH,  // horizontal
  kV,  // vertical
  kNone,
};

class CtsConfig
{
 public:
  CtsConfig() {}
  CtsConfig(const CtsConfig& rhs) = default;
  ~CtsConfig() = default;
  // algorithm
  double get_skew_bound() const { return _skew_bound; }
  double get_max_buf_tran() const { return _max_buf_tran; }
  double get_max_sink_tran() const { return _max_sink_tran; }
  double get_max_cap() const { return _max_cap; }
  int get_max_fanout() const { return _max_fanout; }
  double get_max_length() const { return _max_length; }
  const int& get_h_layer() const { return _h_layer; }
  const int& get_v_layer() const { return _v_layer; }
  std::vector<int> get_routing_layers() const { return _routing_layers; }
  std::vector<std::string> get_buffer_types() const { return _buffer_types; }
  double get_min_buffering_length() const { return _min_buffering_length; }
  // file
  const std::string& get_work_dir() const { return _work_dir; }
  const std::string& get_output_def_path() const { return _output_def_path; }
  const std::string& get_log_file() const { return _log_file; }
  const std::string& get_gds_file() const { return _gds_file; }
  const std::string& get_use_netlist_string() const { return _use_netlist; }
  bool is_use_netlist() { return _use_netlist == "ON" ? true : false; }
  const std::vector<std::pair<std::string, std::string>> get_clock_netlist() const { return _net_list; }
  const std::vector<std::string>& get_deprecated_config_warnings() const { return _deprecated_config_warnings; }

  // algorithm
  void set_skew_bound(double skew_bound) { _skew_bound = skew_bound; }
  void set_max_buf_tran(double max_buf_tran) { _max_buf_tran = max_buf_tran; }
  void set_max_sink_tran(double max_sink_tran) { _max_sink_tran = max_sink_tran; }
  void set_max_cap(double max_cap) { _max_cap = max_cap; }
  void set_max_fanout(int max_fanout) { _max_fanout = max_fanout; }
  void set_max_length(double max_length) { _max_length = max_length; }
  void set_h_layer(const int& h_layer) { _h_layer = h_layer; }
  void set_v_layer(const int& v_layer) { _v_layer = v_layer; }
  void set_routing_layers(const std::vector<int>& routing_layers) { _routing_layers = routing_layers; }
  void set_buffer_types(const std::vector<std::string>& types) { _buffer_types = types; }
  void set_min_buffering_length(const double& min_buffering_length) { _min_buffering_length = min_buffering_length; }

  // file
  void set_work_dir(const std::string& work_dir) { _work_dir = work_dir; }
  void set_output_def_path(const std::string& output_def_path) { _output_def_path = output_def_path; }
  void set_log_file(const std::string& file) { _log_file = file; }
  void set_gds_file(const std::string& file) { _gds_file = file; }
  void set_use_netlist(const std::string& use_netlist) { _use_netlist = use_netlist; }
  void set_netlist(const std::vector<std::pair<std::string, std::string>>& net_list) { _net_list = net_list; }
  void add_deprecated_config_warning(const std::string& warning) { _deprecated_config_warnings.push_back(warning); }

 private:
  // algorithm
  double _skew_bound = 0.04;
  double _max_buf_tran = 1.5;
  double _max_sink_tran = 1.5;
  double _max_cap = 1.5;
  int _max_fanout = 32;
  double _max_length = 300;
  int _h_layer = 1;
  int _v_layer = 1;
  std::vector<int> _routing_layers;
  std::vector<std::string> _buffer_types;
  double _min_buffering_length = 150.0;

  // file
  std::string _work_dir = "./result/cts";
  std::string _output_def_path = "./result/cts/output";
  std::string _log_file = "./result/cts/cts.log";
  std::string _gds_file = "./result/cts/output/cts_design.gds";

  std::string _use_netlist = "OFF";
  std::vector<std::pair<std::string, std::string>> _net_list;
  std::vector<std::string> _deprecated_config_warnings;
};
}  // namespace icts
