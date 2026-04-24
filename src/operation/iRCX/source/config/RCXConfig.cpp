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
#include "RCXConfig.hh"

#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "json/json.hpp"
#include "log/Log.hh"

namespace ircx {

namespace fs = std::filesystem;

namespace {

bool ensure_non_empty(const std::string& value, std::string_view field_name)
{
  if (!value.empty()) {
    return true;
  }

  LOG_ERROR << "RCX config field is empty: " << field_name;
  return false;
}

bool ensure_file_exists(const std::string& path, std::string_view field_name)
{
  if (!ensure_non_empty(path, field_name)) {
    return false;
  }

  if (fs::exists(path)) {
    return true;
  }

  LOG_ERROR << "RCX config file not found for " << field_name << ": " << path;
  return false;
}

std::string resolve_path(const fs::path& config_dir, const std::string& raw_path)
{
  if (raw_path.empty()) {
    return "";
  }

  fs::path path(raw_path);
  if (path.is_relative()) {
    path = config_dir / path;
  }

  return path.lexically_normal().string();
}

}  // namespace

bool RCXConfig::loadFromFile(const std::string& config_path)
{
  if (config_path.empty()) {
    LOG_ERROR << "RCX config path is empty.";
    return false;
  }

  _config_path.clear();
  _thread_num = 64U;
  _output_dir.clear();
  _mapping_file.clear();
  _corner = {};

  std::ifstream config_stream(config_path);
  if (!config_stream.is_open()) {
    LOG_ERROR << "Cannot open RCX config file: " << config_path;
    return false;
  }

  try {
    nlohmann::json json;
    config_stream >> json;

    const fs::path absolute_config_path = fs::absolute(config_path).lexically_normal();
    const fs::path config_dir = absolute_config_path.parent_path();

    if (!json.contains("thread_num") || !json["thread_num"].is_number_integer()) {
      LOG_ERROR << "RCX config missing integer field: thread_num";
      return false;
    }
    const int thread_num = json["thread_num"].get<int>();
    _thread_num = thread_num <= 0 ? 1U : static_cast<unsigned>(thread_num);

    if (json.contains("output") && json["output"].is_string()) {
      _output_dir = resolve_path(config_dir, json["output"].get<std::string>());
    } else {
      _output_dir.clear();
    }

    if (!json.contains("mapping_file") || !json["mapping_file"].is_string()) {
      LOG_ERROR << "RCX config missing string field: mapping_file";
      return false;
    }
    _mapping_file = resolve_path(config_dir, json["mapping_file"].get<std::string>());

    if (!json.contains("corner") || !json["corner"].is_object()) {
      LOG_ERROR << "RCX config missing object field: corner";
      return false;
    }

    const auto& corner_json = json["corner"];
    if (!corner_json.contains("name") || !corner_json["name"].is_string()) {
      LOG_ERROR << "RCX config missing string field: corner.name";
      return false;
    }
    if (!corner_json.contains("itf_file") || !corner_json["itf_file"].is_string()) {
      LOG_ERROR << "RCX config missing string field: corner.itf_file";
      return false;
    }
    if (!corner_json.contains("captab_file") || !corner_json["captab_file"].is_string()) {
      LOG_ERROR << "RCX config missing string field: corner.captab_file";
      return false;
    }

    _corner.name = corner_json["name"].get<std::string>();
    _corner.itf_file = resolve_path(config_dir, corner_json["itf_file"].get<std::string>());
    _corner.captab_file = resolve_path(config_dir, corner_json["captab_file"].get<std::string>());
    _config_path = absolute_config_path.string();

    bool valid = true;
    valid &= ensure_non_empty(_corner.name, "corner.name");
    valid &= ensure_file_exists(_mapping_file, "mapping_file");
    valid &= ensure_file_exists(_corner.itf_file, "corner.itf_file");
    valid &= ensure_file_exists(_corner.captab_file, "corner.captab_file");

    return valid;
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to parse RCX config " << config_path << ": " << e.what();
    return false;
  }
}

}  // namespace ircx
