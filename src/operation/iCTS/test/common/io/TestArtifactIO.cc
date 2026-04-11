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
 * @file TestArtifactIO.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Shared artifact IO and logging helpers for iCTS tests.
 */

#include "common/io/TestArtifactIO.hh"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "common/types/TestDataTypes.hh"
#include "utils/logger/Logger.hh"

namespace icts_test::common::io {
namespace {

auto ResolveExecutableDir() -> std::filesystem::path
{
  std::error_code error_code;
  const auto executable_path = std::filesystem::canonical("/proc/self/exe", error_code);
  if (error_code || executable_path.empty()) {
    return {};
  }
  return executable_path.parent_path();
}

}  // namespace

auto WriteTextLog(const std::filesystem::path& path, const std::string& content) -> bool
{
  std::error_code error_code;
  const auto parent_dir = path.parent_path();
  if (!parent_dir.empty()) {
    std::filesystem::create_directories(parent_dir, error_code);
    if (error_code) {
      return false;
    }
  }

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }
  output_stream << content;
  return true;
}

void EmitInfoReport(const InfoReport& report)
{
  CTS_LOG_INFO << "[" << report.title << "] report_begin";

  std::istringstream input_stream(report.content);
  for (std::string line; std::getline(input_stream, line);) {
    if (line.empty()) {
      continue;
    }
    CTS_LOG_INFO << "[" << report.title << "] " << line;
  }

  CTS_LOG_INFO << "[" << report.title << "] report_end";
}

auto SanitizeOutputName(const std::string& raw_name) -> std::string
{
  std::string sanitized;
  sanitized.reserve(raw_name.size());

  bool previous_was_separator = false;
  for (const char value : raw_name) {
    const auto character = static_cast<unsigned char>(value);
    if (std::isalnum(character) != 0) {
      sanitized.push_back(static_cast<char>(std::tolower(character)));
      previous_was_separator = false;
      continue;
    }

    if (!previous_was_separator && !sanitized.empty()) {
      sanitized.push_back('_');
      previous_was_separator = true;
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_') {
    sanitized.pop_back();
  }
  return sanitized.empty() ? "unnamed" : sanitized;
}

auto PrepareCleanOutputDir(const std::filesystem::path& path) -> std::filesystem::path
{
  std::error_code error_code;
  std::filesystem::remove_all(path, error_code);
  if (error_code) {
    return {};
  }

  error_code.clear();
  std::filesystem::create_directories(path, error_code);
  if (error_code) {
    return {};
  }
  return path;
}

auto ResolveOutputDir() -> std::filesystem::path
{
  const char* env_dir = std::getenv("ICTS_TEST_OUTPUT_DIR");
  if (env_dir != nullptr && *env_dir != '\0') {
    return {env_dir};
  }

  // Default to an executable-adjacent output root so test artifacts stay stable
  // regardless of the caller's current working directory.
  const auto executable_dir = ResolveExecutableDir();
  if (!executable_dir.empty()) {
    return executable_dir / "icts_test_output";
  }

  return {"icts_test_output"};
}

auto ResolveTopologyGenOutputDir() -> std::filesystem::path
{
  return ResolveOutputDir() / "topology_gen";
}

auto ResolveLinearClusteringOutputDir() -> std::filesystem::path
{
  return ResolveOutputDir() / "linear_clustering";
}

}  // namespace icts_test::common::io
