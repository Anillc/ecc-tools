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
 * @file JsonParser.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "JsonParser.hh"

#include <array>

#include "COMUtil.hh"
#include "json/json.hpp"
#include "log/Log.hh"

namespace {

void warnDeprecatedConfigKey(const nlohmann::json& json, icts::CtsConfig* config, const char* key, const std::string& message)
{
  if (!json.contains(key)) {
    return;
  }

  const std::string warning = "Deprecated CTS config key \"" + std::string(key) + "\" is ignored: " + message;
  LOG_WARNING << warning;
  config->add_deprecated_config_warning(warning);
}

void warnDeprecatedConfigKeys(const nlohmann::json& json, icts::CtsConfig* config)
{
  static const std::array<std::pair<const char*, const char*>, 21> k_deprecated_keys = {{
      {"use_skew_tree_alg", "the skew-tree algorithm switch has been removed from the active CTS flow."},
      {"min_length", "the min-length driven clustering path has been removed."},
      {"max_length", "the max-length CTS config knob is deprecated and ignored in the active CTS flow."},
      {"router_type", "legacy router selection is no longer supported."},
      {"delay_type", "legacy delay model selection is no longer supported."},
      {"cluster_type", "legacy clustering mode selection is no longer supported."},
      {"scale_size", "legacy scaling control is no longer supported."},
      {"cluster_size", "legacy fixed cluster-size control is no longer supported."},
      {"root_buffer_type", "root buffer sizing is now derived from the unified level sizing search."},
      {"root_buffer_required", "the H-tree root buffer is always enabled."},
      {"inherit_root", "root inheritance control has been removed."},
      {"break_long_wire", "long-wire buffering is always enabled."},
      {"latency_opt_level", "legacy latency optimization knobs have been removed."},
      {"global_latency_opt_ratio", "legacy latency optimization knobs have been removed."},
      {"local_latency_opt_ratio", "legacy latency optimization knobs have been removed."},
      {"level_max_length", "per-level max-length constraints have been removed from the active CTS flow."},
      {"level_max_fanout", "per-level max-fanout constraints have been removed from the active CTS flow."},
      {"level_max_cap", "per-level max-cap constraints have been removed from the active CTS flow."},
      {"level_skew_bound", "per-level skew constraints have been removed from the active CTS flow."},
      {"level_cluster_ratio", "per-level cluster-ratio constraints have been removed from the active CTS flow."},
      {"shift_level", "shift-based centroid relocation has been removed from the active CTS flow."},
  }};

  for (const auto& [key, message] : k_deprecated_keys) {
    warnDeprecatedConfigKey(json, config, key, message);
  }

  warnDeprecatedConfigKey(json, config, "external_model", "external-model integration is no longer supported in the active CTS flow.");
}

}  // namespace

namespace icts {

JsonParser::JsonParser()
{
  char program_name[] = "JsonParser";
  char* argv[] = {program_name};
  // We need to initialize the log system here, because JsonParser() may be called in pybind,
  // which does not have a main function to initialize the log system.
  if (!ieda::Log::isInit()) {
    const std::string log_dir = "/tmp/ieda_logs/";
    ieda::Log::makeSureDirectoryExist(log_dir);
    ieda::Log::init(argv, log_dir);
  }
}

JsonParser& JsonParser::getInstance()
{
  static JsonParser parser;
  return parser;
}

void JsonParser::parse(const string& json_file, CtsConfig* config) const
{
  std::ifstream& ifs = COMUtil::getInputFileStream(json_file);
  if (!ifs) {
    LOG_FATAL << "no config file: " << json_file;
  } else {
    LOG_INFO << "read config success : " << json_file;
  }
  nlohmann::json json;
  std::string data_type_string = "string";

  ifs >> json;

  {
    warnDeprecatedConfigKeys(json, config);
    if (COMUtil::getData(json, {"skew_bound"}) != nullptr) {
      std::string skew_bound = COMUtil::getData(json, {"skew_bound"});
      config->set_skew_bound(std::stod(skew_bound));
    }
    if (COMUtil::getData(json, {"max_buf_tran"}) != nullptr) {
      std::string max_buf_tran = COMUtil::getData(json, {"max_buf_tran"});
      config->set_max_buf_tran(std::stod(max_buf_tran));
    }
    if (COMUtil::getData(json, {"max_sink_tran"}) != nullptr) {
      std::string max_sink_tran = COMUtil::getData(json, {"max_sink_tran"});
      config->set_max_sink_tran(std::stod(max_sink_tran));
    }
    if (COMUtil::getData(json, {"max_cap"}) != nullptr) {
      std::string max_cap = COMUtil::getData(json, {"max_cap"});
      config->set_max_cap(std::stod(max_cap));
    }
    if (COMUtil::getData(json, {"max_fanout"}) != nullptr) {
      std::string max_fanout = COMUtil::getData(json, {"max_fanout"});
      config->set_max_fanout(std::stoi(max_fanout));
    }
    if (COMUtil::getData(json, {"routing_layer"}) != nullptr) {
      std::vector<int> routing_layers = COMUtil::getData(json, {"routing_layer"});
      config->set_routing_layers(routing_layers);
      config->set_h_layer(routing_layers.front());
      config->set_v_layer(routing_layers.back());
    }
    if (COMUtil::getData(json, {"buffer_type"}) != nullptr) {
      config->set_buffer_types(COMUtil::getData(json, {"buffer_type"}));
    }
    if (COMUtil::getData(json, {"min_buffering_length"}) != nullptr) {
      std::string min_buffering_length = COMUtil::getData(json, {"min_buffering_length"});
      config->set_min_buffering_length(std::stod(min_buffering_length));
    }

    if (COMUtil::getData(json, {"use_netlist"}) != nullptr) {
      config->set_use_netlist(COMUtil::getData(json, {"use_netlist"}));
    }
    nlohmann::json json_netlist = COMUtil::getData(json, {"net_list"});
    {
      auto clock_name_list = COMUtil::getSerializeObjectData(json_netlist, "clock_name", data_type_string);
      auto net_name_list = COMUtil::getSerializeObjectData(json_netlist, "net_name", data_type_string);

      int size = clock_name_list.size();
      std::vector<std::pair<std::string, std::string>> clock_net_list;
      for (int i = 0; i < size; ++i) {
        clock_net_list.push_back(std::make_pair(clock_name_list[i], net_name_list[i]));
      }

      config->set_netlist(clock_net_list);
    }
  }

  ifs.close();
}

std::string JsonParser::resolvePath(const std::string& path) const
{
  std::string resolved_path = path;
  size_t start_pos = resolved_path.find('$');
  while (start_pos != std::string::npos) {
    size_t end_pos = resolved_path.find('/', start_pos);
    if (end_pos == std::string::npos) {
      end_pos = resolved_path.length();
    }

    std::string var = resolved_path.substr(start_pos + 1, end_pos - start_pos - 1);
    const char* env_val = std::getenv(var.c_str());
    if (env_val != nullptr) {
      resolved_path.replace(start_pos, end_pos - start_pos, env_val);
    }

    start_pos = resolved_path.find('$', start_pos + 1);
  }

  return resolved_path;
}

}  // namespace icts
