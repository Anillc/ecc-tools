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
#include "database/config/Config.hh"

#include <glog/logging.h>

#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "LogFormat.hh"
#include "json.hpp"
#include "logger/Schema.hh"

namespace icts {
namespace {

auto parse_bool(const nlohmann::json& value, bool default_value) -> bool
{
  if (value.is_boolean()) {
    return value.get<bool>();
  }
  if (value.is_number_integer()) {
    return value.get<int>() != 0;
  }
  if (value.is_string()) {
    auto str = value.get<std::string>();
    for (auto& character : str) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return str == "true" || str == "on" || str == "yes" || str == "1";
  }
  return default_value;
}

auto parse_double(const nlohmann::json& value, double default_value) -> double
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

auto parse_unsigned(const nlohmann::json& value, unsigned default_value) -> unsigned
{
  constexpr auto unsigned_max = static_cast<unsigned long>(std::numeric_limits<unsigned>::max());

  if (value.is_number_integer()) {
    const auto parsed = value.get<long long>();
    if (parsed < 0) {
      return default_value;
    }
    const auto parsed_unsigned = static_cast<unsigned long long>(parsed);
    if (parsed_unsigned > unsigned_max) {
      return default_value;
    }
    return static_cast<unsigned>(parsed_unsigned);
  }
  if (value.is_number_float()) {
    const auto parsed = value.get<double>();
    if (parsed < 0.0 || parsed > static_cast<double>(std::numeric_limits<unsigned>::max())) {
      return default_value;
    }
    return static_cast<unsigned>(parsed);
  }
  if (value.is_string()) {
    try {
      const auto parsed = std::stoul(value.get<std::string>());
      if (parsed > unsigned_max) {
        return default_value;
      }
      return static_cast<unsigned>(parsed);
    } catch (...) {
      return default_value;
    }
  }
  return default_value;
}

auto parse_string(const nlohmann::json& value, const std::string& default_value) -> std::string
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

auto parse_unsigned_list(const nlohmann::json& value) -> std::vector<unsigned>
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

auto parse_string_list(const nlohmann::json& value) -> std::vector<std::string>
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

auto ApplyDoubleIfPresent(const nlohmann::json& json, const char* key, Config& config, double (Config::*getter)() const,
                          void (Config::*setter)(double)) -> void
{
  if (json.contains(key)) {
    (config.*setter)(parse_double(json.at(key), (config.*getter)()));
  }
}

auto ApplyUnsignedIfPresent(const nlohmann::json& json, const char* key, Config& config, unsigned (Config::*getter)() const,
                            void (Config::*setter)(unsigned)) -> void
{
  if (json.contains(key)) {
    (config.*setter)(parse_unsigned(json.at(key), (config.*getter)()));
  }
}

auto ApplyBoolIfPresent(const nlohmann::json& json, const char* key, bool default_value, Config& config, void (Config::*setter)(bool))
    -> void
{
  if (json.contains(key)) {
    (config.*setter)(parse_bool(json.at(key), default_value));
  }
}

auto ApplyRoutingLayersIfPresent(const nlohmann::json& json, Config& config) -> void
{
  if (json.contains("routing_layer")) {
    auto routing_layers = parse_unsigned_list(json.at("routing_layer"));
    if (!routing_layers.empty()) {
      config.set_routing_layers(routing_layers);
    }
  }
}

auto ApplyBufferTypesIfPresent(const nlohmann::json& json, Config& config) -> void
{
  if (json.contains("buffer_type")) {
    config.set_buffer_types(parse_string_list(json.at("buffer_type")));
  }
}

auto ParseNetList(const nlohmann::json& value) -> std::vector<std::pair<std::string, std::string>>
{
  std::vector<std::pair<std::string, std::string>> clock_net_list;
  if (!value.is_array()) {
    return clock_net_list;
  }

  for (const auto& item : value) {
    if (!item.is_object()) {
      continue;
    }
    if (!item.contains("clock_name") || !item.contains("net_name")) {
      continue;
    }

    auto clock_name = parse_string(item.at("clock_name"), "");
    auto net_name = parse_string(item.at("net_name"), "");
    if (!clock_name.empty() && !net_name.empty()) {
      clock_net_list.emplace_back(clock_name, net_name);
    }
  }

  return clock_net_list;
}

auto ApplyNetListIfPresent(const nlohmann::json& json, Config& config) -> void
{
  if (json.contains("net_list") && json.at("net_list").is_array()) {
    config.set_net_list(ParseNetList(json.at("net_list")));
  }
}

}  // namespace

auto Config::init(const std::string& config_file) -> void
{
  reset();
  parse(config_file);
}

auto Config::parse(const std::string& json_file) -> void
{
  std::ifstream ifs(json_file);
  if (!ifs) {
    LOG_ERROR << "Failed to open iCTS config file: " << json_file;
    return;
  }

  nlohmann::json json;
  ifs >> json;

  ApplyDoubleIfPresent(json, "skew_bound", *this, &Config::get_skew_bound, &Config::set_skew_bound);
  ApplyDoubleIfPresent(json, "max_buf_tran", *this, &Config::get_max_buf_tran, &Config::set_max_buf_tran);
  ApplyDoubleIfPresent(json, "max_sink_tran", *this, &Config::get_max_sink_tran, &Config::set_max_sink_tran);
  ApplyDoubleIfPresent(json, "max_cap", *this, &Config::get_max_cap, &Config::set_max_cap);
  // max_length remains parseable as a placeholder knob; active lattice comes
  // from wire_length_unit_um + wire_length_iterations.
  ApplyDoubleIfPresent(json, "max_length", *this, &Config::get_max_length, &Config::set_max_length);
  ApplyDoubleIfPresent(json, "wire_length_unit_um", *this, &Config::get_wire_length_unit_um, &Config::set_wire_length_unit_um);
  ApplyUnsignedIfPresent(json, "wire_length_iterations", *this, &Config::get_wire_length_iterations, &Config::set_wire_length_iterations);
  ApplyUnsignedIfPresent(json, "slew_steps", *this, &Config::get_slew_steps, &Config::set_slew_steps);
  ApplyUnsignedIfPresent(json, "cap_steps", *this, &Config::get_cap_steps, &Config::set_cap_steps);
  ApplyUnsignedIfPresent(json, "relaxed_candidates_per_boundary_group", *this, &Config::get_relaxed_candidates_per_boundary_group,
                         &Config::set_relaxed_candidates_per_boundary_group);
  ApplyDoubleIfPresent(json, "wire_width", *this, &Config::get_wire_width, &Config::set_wire_width);
  ApplyUnsignedIfPresent(json, "max_fanout", *this, &Config::get_max_fanout, &Config::set_max_fanout);
  ApplyRoutingLayersIfPresent(json, *this);
  ApplyBufferTypesIfPresent(json, *this);
  ApplyDoubleIfPresent(json, "char_buf_redundancy_pct", *this, &Config::get_char_buf_redundancy_pct, &Config::set_char_buf_redundancy_pct);
  const bool has_force_branch_buffer = json.contains("force_branch_buffer");
  const bool has_force_leaf_branch_buffer = json.contains("force_leaf_branch_buffer");
  const std::optional<bool> parsed_force_branch_buffer
      = has_force_branch_buffer ? std::make_optional(parse_bool(json.at("force_branch_buffer"), false)) : std::nullopt;
  const std::optional<bool> parsed_force_leaf_branch_buffer
      = has_force_leaf_branch_buffer ? std::make_optional(parse_bool(json.at("force_leaf_branch_buffer"), false)) : std::nullopt;
  if (parsed_force_branch_buffer.has_value()) {
    if (parsed_force_leaf_branch_buffer.has_value() && *parsed_force_branch_buffer != *parsed_force_leaf_branch_buffer) {
      LOG_WARNING << "Config: both force_branch_buffer and deprecated force_leaf_branch_buffer are present; "
                     "using force_branch_buffer.";
    }
    set_force_branch_buffer(*parsed_force_branch_buffer);
  } else if (parsed_force_leaf_branch_buffer.has_value()) {
    set_force_branch_buffer(*parsed_force_leaf_branch_buffer);
  }
  ApplyBoolIfPresent(json, "use_netlist", false, *this, &Config::set_use_netlist);
  ApplyNetListIfPresent(json, *this);
}

auto Config::buildRuntimeConfigRows() const -> logformat::TableRows
{
  const auto& routing_layers = get_routing_layers();
  const auto& buffer_types = get_buffer_types();
  const bool has_wire_length_unit = get_wire_length_unit_um() > 0.0;
  const bool has_wire_width = get_wire_width() > 0.0;
  const bool has_relaxed_limit = get_relaxed_candidates_per_boundary_group() > 0U;

  return {
      {"skew_bound_ns", logformat::FormatWithUnit(get_skew_bound(), "ns"), "clock skew target"},
      {"max_buf_tran_ns", has_max_buf_tran() ? logformat::FormatWithUnit(get_max_buf_tran(), "ns") : "auto",
       has_max_buf_tran() ? "explicit runtime config" : "resolve from liberty slew limit/table-axis during characterization"},
      {"max_sink_tran_ns", logformat::FormatWithUnit(get_max_sink_tran(), "ns"), "sink transition target"},
      {"max_cap_pf", has_max_cap() ? logformat::FormatWithUnit(get_max_cap(), "pF") : "auto",
       has_max_cap() ? "explicit runtime config" : "resolve from liberty cap limit/table-axis during characterization"},
      {"max_length_um", logformat::FormatWithUnit(get_max_length(), "um"), "legacy compatibility knob; not the active step lattice"},
      {"wire_length_unit_um", has_wire_length_unit ? logformat::FormatWithUnit(get_wire_length_unit_um(), "um") : "auto",
       has_wire_length_unit ? "active characterization lattice unit" : "fallback/override resolves later in flow"},
      {"wire_length_iterations", std::to_string(get_wire_length_iterations()), "characterization length bins"},
      {"slew_steps", std::to_string(get_slew_steps()), "characterization slew bins"},
      {"cap_steps", std::to_string(get_cap_steps()), "characterization load-cap bins"},
      {"relaxed_candidates_per_boundary_group",
       has_relaxed_limit ? std::to_string(get_relaxed_candidates_per_boundary_group()) : "0 (unlimited)",
       "frontier composition throttling"},
      {"wire_width_um", has_wire_width ? logformat::FormatWithUnit(get_wire_width(), "um") : "library_default",
       has_wire_width ? "explicit RC width override" : "use technology default width in RC query"},
      {"max_fanout", std::to_string(get_max_fanout()), "fanout constraint"},
      {"routing_layers", routing_layers.empty() ? "default(1)" : std::to_string(routing_layers.front()),
       routing_layers.empty() ? "effective routing layer falls back to 1" : "configured order: " + logformat::JoinUnsigned(routing_layers)},
      {"buffer_types", std::to_string(buffer_types.size()),
       buffer_types.empty() ? "no configured buffers" : logformat::JoinStrings(buffer_types)},
      {"char_buf_redundancy_pct", logformat::FormatPercent(get_char_buf_redundancy_pct()),
       get_char_buf_redundancy_pct() > 0.0 ? "near-neighbor max-cap pruning threshold" : "disabled"},
      {"force_branch_buffer", logformat::FormatBool(is_force_branch_buffer()),
       is_force_branch_buffer() ? "require terminal-buffered segment frontiers on every H-tree level" : "disabled"},
      {"use_netlist", logformat::FormatBool(is_use_netlist()),
       is_use_netlist() ? "configured net pairs: " + std::to_string(get_net_list().size()) : "clock net pairs collected from STA"},
  };
}

auto Config::emitRuntimeConfigReport(const std::string& title) const -> void
{
  schema::EmitTable(title, {"Item", "Value", "Detail"}, buildRuntimeConfigRows());
}

}  // namespace icts
