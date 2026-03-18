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
 * @file Config.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief Configuration parser for iCTS.
 */
#include "Config.hh"

#include <cctype>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"
#include "logger/Logger.hh"

namespace icts {
namespace {

bool parse_bool(const nlohmann::json& value, bool default_value)
{
  if (value.is_boolean()) {
    return value.get<bool>();
  }
  if (value.is_number_integer()) {
    return value.get<int>() != 0;
  }
  if (value.is_string()) {
    auto str = value.get<std::string>();
    for (auto& ch : str) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return str == "true" || str == "on" || str == "yes" || str == "1";
  }
  return default_value;
}

double parse_double(const nlohmann::json& value, double default_value)
{
  if (value.is_number_float() || value.is_number_integer()) {
    return value.get<double>();
  }
  if (value.is_string()) {
    try {
      return std::stod(value.get<std::string>());
    } catch (...) {
      return default_value;
    }
  }
  return default_value;
}

unsigned parse_unsigned(const nlohmann::json& value, unsigned default_value)
{
  if (value.is_number_integer()) {
    return value.get<unsigned>();
  }
  if (value.is_number_float()) {
    return static_cast<unsigned>(value.get<double>());
  }
  if (value.is_string()) {
    try {
      return std::stoul(value.get<std::string>());
    } catch (...) {
      return default_value;
    }
  }
  return default_value;
}

std::string parse_string(const nlohmann::json& value, const std::string& default_value)
{
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<int>());
  }
  if (value.is_number_float()) {
    return std::to_string(value.get<double>());
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  return default_value;
}

std::vector<unsigned> parse_unsigned_list(const nlohmann::json& value)
{
  std::vector<unsigned> result;
  if (!value.is_array()) {
    return result;
  }
  for (const auto& item : value) {
    result.push_back(parse_unsigned(item, 0));
  }
  return result;
}

std::vector<std::string> parse_string_list(const nlohmann::json& value)
{
  std::vector<std::string> result;
  if (!value.is_array()) {
    return result;
  }
  for (const auto& item : value) {
    result.push_back(parse_string(item, ""));
  }
  return result;
}

}  // namespace

void Config::parse(const std::string& json_file)
{
  std::ifstream ifs(json_file);
  if (!ifs) {
    CTS_LOG_ERROR << "Failed to open iCTS config file: " << json_file;
    return;
  }

  nlohmann::json json;
  ifs >> json;

  if (json.contains("skew_bound")) {
    set_skew_bound(parse_double(json["skew_bound"], get_skew_bound()));
  }
  if (json.contains("max_buf_tran")) {
    set_max_buf_tran(parse_double(json["max_buf_tran"], get_max_buf_tran()));
  }
  if (json.contains("max_sink_tran")) {
    set_max_sink_tran(parse_double(json["max_sink_tran"], get_max_sink_tran()));
  }
  if (json.contains("max_cap")) {
    set_max_cap(parse_double(json["max_cap"], get_max_cap()));
  }
  if (json.contains("max_length")) {
    set_max_length(parse_double(json["max_length"], get_max_length()));
  }
  if (json.contains("slew_steps")) {
    set_slew_steps(parse_unsigned(json["slew_steps"], get_slew_steps()));
  }
  if (json.contains("cap_steps")) {
    set_cap_steps(parse_unsigned(json["cap_steps"], get_cap_steps()));
  }
  if (json.contains("length_steps")) {
    set_length_steps(parse_unsigned(json["length_steps"], get_length_steps()));
  }
  if (json.contains("max_pattern_nodes")) {
    set_max_pattern_nodes(parse_unsigned(json["max_pattern_nodes"], get_max_pattern_nodes()));
  }
  if (json.contains("wire_width")) {
    set_wire_width(parse_double(json["wire_width"], get_wire_width()));
  }
  if (json.contains("max_fanout")) {
    set_max_fanout(parse_unsigned(json["max_fanout"], get_max_fanout()));
  }
  if (json.contains("routing_layer")) {
    auto routing_layers = parse_unsigned_list(json["routing_layer"]);
    if (!routing_layers.empty()) {
      set_routing_layers(routing_layers);
    }
  }
  if (json.contains("buffer_type")) {
    set_buffer_types(parse_string_list(json["buffer_type"]));
  }
  if (json.contains("char_buf_redundancy_pct")) {
    set_char_buf_redundancy_pct(parse_double(json["char_buf_redundancy_pct"], get_char_buf_redundancy_pct()));
  }

  if (json.contains("use_netlist")) {
    set_use_netlist(parse_bool(json["use_netlist"], false));
  }

  if (json.contains("net_list") && json["net_list"].is_array()) {
    std::vector<std::pair<std::string, std::string>> clock_net_list;
    for (const auto& item : json["net_list"]) {
      if (!item.is_object()) {
        continue;
      }
      if (!item.contains("clock_name") || !item.contains("net_name")) {
        continue;
      }
      auto clock_name = parse_string(item["clock_name"], "");
      auto net_name = parse_string(item["net_name"], "");
      if (!clock_name.empty() && !net_name.empty()) {
        clock_net_list.emplace_back(clock_name, net_name);
      }
    }
    set_net_list(clock_net_list);
  }
}

}  // namespace icts
