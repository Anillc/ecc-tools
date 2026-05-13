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
#include "tcl_ctsconfig.h"

#include <any>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tcl_util.h"

namespace tcl {
namespace {

using ConfigOption = std::pair<std::string, ValueType>;

auto MakeSupportedCtsConfigOptions() -> std::vector<ConfigOption>
{
  return {
      {"-config_json_path", ValueType::kString},
      {"-skew_bound", ValueType::kDouble},
      {"-max_buf_tran", ValueType::kDouble},
      {"-root_input_slew", ValueType::kDouble},
      {"-max_sink_tran", ValueType::kDouble},
      {"-max_cap", ValueType::kDouble},
      {"-max_length", ValueType::kDouble},
      {"-wirelength_unit_um", ValueType::kDouble},
      {"-wirelength_iterations", ValueType::kInt},
      {"-slew_steps", ValueType::kInt},
      {"-cap_steps", ValueType::kInt},
      {"-wire_width", ValueType::kDouble},
      {"-max_fanout", ValueType::kInt},
      {"-routing_layer", ValueType::kIntList},
      {"-buffer_type", ValueType::kStringList},
      {"-char_buf_redundancy_pct", ValueType::kDouble},
      {"-force_branch_buffer", ValueType::kString},
      {"-htree_depth_explore_window", ValueType::kInt},
      {"-htree_topology_tolerance", ValueType::kDouble},
      {"-enable_sink_clustering", ValueType::kString},
      {"-use_netlist", ValueType::kString},
      {"-net_list", ValueType::kStringList},
  };
}

auto Trim(const std::string& value) -> std::string
{
  const auto first = value.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\n\r");
  return value.substr(first, last - first + 1);
}

auto FindNetListDelimiter(const std::string& entry) -> std::string::size_type
{
  const auto colon_pos = entry.find(':');
  if (colon_pos != std::string::npos) {
    return colon_pos;
  }
  return entry.find(',');
}

auto BuildNetListJson(const std::vector<std::string>& entries, ordered_json& net_list_json) -> bool
{
  net_list_json = ordered_json::array();
  for (const auto& entry : entries) {
    const auto delimiter_pos = FindNetListDelimiter(entry);
    if (delimiter_pos == std::string::npos) {
      std::cerr << "Invalid CTS net_list entry: " << entry << ". Expected clock_name:net_name." << std::endl;
      return false;
    }

    const auto clock_name = Trim(entry.substr(0, delimiter_pos));
    const auto net_name = Trim(entry.substr(delimiter_pos + 1));
    if (clock_name.empty() || net_name.empty()) {
      std::cerr << "Invalid CTS net_list entry: " << entry << ". Clock name and net name must be non-empty." << std::endl;
      return false;
    }

    ordered_json net_pair;
    net_pair["clock_name"] = clock_name;
    net_pair["net_name"] = net_name;
    net_list_json.push_back(std::move(net_pair));
  }
  return true;
}

auto UpdateCtsNetListConfig(const std::string& config_json_path, const std::vector<std::string>& entries) -> bool
{
  ordered_json config;
  {
    std::ifstream input_stream(config_json_path);
    if (!input_stream.is_open()) {
      std::cerr << "Failed to open CTS JSON file for net_list update: " << config_json_path << std::endl;
      return false;
    }
    input_stream >> config;
  }

  ordered_json net_list_json;
  if (!BuildNetListJson(entries, net_list_json)) {
    return false;
  }
  config["net_list"] = std::move(net_list_json);

  std::ofstream output_stream(config_json_path);
  if (!output_stream.is_open()) {
    std::cerr << "Failed to write CTS JSON file for net_list update: " << config_json_path << std::endl;
    return false;
  }
  output_stream << std::setw(4) << config;
  return true;
}

}  // namespace

CmdCTSConfig::CmdCTSConfig(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list = MakeSupportedCtsConfigOptions();
  TclUtil::addOption(this, _config_list);
}

unsigned CmdCTSConfig::exec()
{
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  if (config_map.empty()) {
    return 0;
  }

  const auto config_path_iter = config_map.find("-config_json_path");
  if (config_path_iter == config_map.end()) {
    std::cerr << "CTS config update requires -config_json_path." << std::endl;
    return 0;
  }
  const auto config_json_path = std::any_cast<std::string>(config_path_iter->second);
  config_map.erase(config_path_iter);

  std::vector<std::string> net_list_entries;
  bool update_net_list = false;
  const auto net_list_iter = config_map.find("-net_list");
  if (net_list_iter != config_map.end()) {
    net_list_entries = std::any_cast<std::vector<std::string>>(net_list_iter->second);
    update_net_list = true;
    config_map.erase(net_list_iter);
  }

  if (!config_map.empty() && !TclUtil::alterJsonConfig(config_json_path, config_map)) {
    return 0;
  }
  if (update_net_list && !UpdateCtsNetListConfig(config_json_path, net_list_entries)) {
    return 0;
  }
  return 1;
}

}  // namespace tcl
