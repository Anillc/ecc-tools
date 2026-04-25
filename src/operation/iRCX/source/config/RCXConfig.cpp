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

std::string trim_copy(const std::string& value)
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return "";
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1);
}

std::string resolve_path(const fs::path& config_dir, const std::string& raw_path)
{
  const std::string trimmed_path = trim_copy(raw_path);
  if (trimmed_path.empty()) {
    return "";
  }

  fs::path path(trimmed_path);
  if (path.is_relative()) {
    path = config_dir / path;
  }

  return path.lexically_normal().string();
}

bool parse_corner_config(const nlohmann::json& corner_json,
                         const fs::path& config_dir,
                         std::string_view field_name,
                         RCXConfig::CornerConfig& corner_config)
{
  if (!corner_json.is_object()) {
    LOG_ERROR << "RCX config field must be an object: " << field_name;
    return false;
  }

  const std::string field_name_str(field_name);
  const std::string name_field = field_name_str + ".name";
  const std::string ecc_tf_field = field_name_str + ".ecc_tf";
  const std::string itf_field = field_name_str + ".itf_file";
  const std::string captab_field = field_name_str + ".captab_file";

  if (!corner_json.contains("name") || !corner_json["name"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << name_field;
    return false;
  }
  if (corner_json.contains("ecc_tf") && !corner_json["ecc_tf"].is_string()) {
    LOG_ERROR << "RCX config field must be a string: " << ecc_tf_field;
    return false;
  }
  if (!corner_json.contains("itf_file") || !corner_json["itf_file"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << itf_field;
    return false;
  }
  if (!corner_json.contains("captab_file") || !corner_json["captab_file"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << captab_field;
    return false;
  }

  corner_config.name = trim_copy(corner_json["name"].get<std::string>());
  corner_config.ecc_tf = corner_json.contains("ecc_tf")
                             ? resolve_path(config_dir, corner_json["ecc_tf"].get<std::string>())
                             : "";
  corner_config.itf_file = resolve_path(config_dir, corner_json["itf_file"].get<std::string>());
  corner_config.captab_file = resolve_path(config_dir, corner_json["captab_file"].get<std::string>());

  bool valid = true;
  valid &= ensure_non_empty(corner_config.name, name_field);
  valid &= ensure_file_exists(corner_config.itf_file, itf_field);
  valid &= ensure_file_exists(corner_config.captab_file, captab_field);

  return valid;
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
  _corners.clear();

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

    if (!json.contains("corners")) {
      LOG_ERROR << "RCX config missing field: corners";
      return false;
    }

    const auto& corners_json = json["corners"];
    bool valid = true;

    if (corners_json.is_array()) {
      if (corners_json.empty()) {
        LOG_ERROR << "RCX config field corners must not be empty.";
        return false;
      }

      _corners.reserve(corners_json.size());
      for (size_t idx = 0; idx < corners_json.size(); ++idx) {
        CornerConfig corner_config;
        valid &= parse_corner_config(corners_json[idx], config_dir,
                                     "corner[" + std::to_string(idx) + "]", corner_config);
        _corners.emplace_back(std::move(corner_config));
      }
    } else if (corners_json.is_object()) {
      CornerConfig corner_config;
      valid &= parse_corner_config(corners_json, config_dir, "corner", corner_config);
      _corners.emplace_back(std::move(corner_config));
    } else {
      LOG_ERROR << "RCX config field corners must be an object or array.";
      return false;
    }

    _config_path = absolute_config_path.string();

    valid &= ensure_file_exists(_mapping_file, "mapping_file");

    return valid;
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to parse RCX config " << config_path << ": " << e.what();
    return false;
  }
}

}  // namespace ircx
