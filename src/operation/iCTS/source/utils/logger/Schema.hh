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
 * @file Schema.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-16
 * @brief Structured report schema writer for iCTS runtime and test artifacts.
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "LogFormat.hh"

namespace icts::schema {

using KeyValueFields = std::vector<std::pair<std::string, std::string>>;
using TableRows = logformat::TableRows;

enum class DiagnosticLevel
{
  kInfo,
  kWarning,
  kError,
  kFallback
};

class SchemaWriter
{
 public:
  static auto getInst() -> SchemaWriter&
  {
    static SchemaWriter inst;
    return inst;
  }

  auto open(const std::filesystem::path& path, const std::string& run_title, const KeyValueFields& metadata = {}) -> void;
  auto close() -> void;
  auto isOpen() const -> bool;
  auto getActivePath() const -> std::filesystem::path;

  auto emitSection(const std::string& title) -> void;
  auto emitTable(const std::string& title, const std::vector<std::string>& headers, const TableRows& rows) -> void;
  auto emitKeyValueTable(const std::string& title, const KeyValueFields& fields) -> void;
  auto emitDetailBlock(const std::string& title, const std::vector<std::string>& lines) -> void;
  auto emitDiagnostic(DiagnosticLevel level, const std::string& owner, const std::string& summary, const KeyValueFields& fields = {})
      -> void;
  auto emitArtifact(const std::string& label, const std::filesystem::path& path, const std::string& detail = {}) -> void;

  static auto appendStandaloneTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                    const std::vector<std::string>& headers, const TableRows& rows) -> void;
  static auto appendStandaloneKeyValueTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                            const KeyValueFields& fields) -> void;
  static auto appendStandaloneDetailBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                          const std::vector<std::string>& lines) -> void;
  static auto appendStandaloneArtifact(const std::filesystem::path& path, const std::string& run_title, const std::string& label,
                                       const std::filesystem::path& artifact_path, const std::string& detail = {}) -> void;
  static auto emitOrAppendKeyValueTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                        const KeyValueFields& fields) -> void;
  static auto emitOrAppendDetailBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                      const std::vector<std::string>& lines) -> void;

 private:
  struct SuspendedWriter
  {
    std::filesystem::path path;
    bool has_content = false;
  };

  SchemaWriter() = default;
  ~SchemaWriter() = default;

  auto writeBlockLocked(const std::string& block) -> void;
  auto restoreSuspendedWriterLocked() -> void;
  static auto appendStandaloneBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& block) -> void;

  mutable std::mutex _mutex;
  std::ofstream _stream;
  std::filesystem::path _path;
  bool _has_content = false;
  std::vector<SuspendedWriter> _suspended_writers;
};

#define SCHEMA_WRITER_INST (icts::schema::SchemaWriter::getInst())

class ScopedStage
{
 public:
  ScopedStage(std::string module, std::string stage, const KeyValueFields& start_fields = {});
  ~ScopedStage();

  auto markRunning(const std::string& summary, const KeyValueFields& fields = {}) -> void;
  auto finish(const KeyValueFields& finish_fields = {}, std::string outcome = "success") -> void;
  auto skip(const KeyValueFields& finish_fields = {}, std::string outcome = "skipped") -> void;

 private:
  std::string _module;
  std::string _stage;
  std::chrono::steady_clock::time_point _start_time;
  bool _finished = false;
};

auto EmitTable(const std::string& title, const std::vector<std::string>& headers, const TableRows& rows) -> void;
auto EmitKeyValueTable(const std::string& title, const KeyValueFields& fields) -> void;
auto EmitDiagnostic(DiagnosticLevel level, const std::string& owner, const std::string& summary, const KeyValueFields& fields = {}) -> void;
auto EmitArtifact(const std::string& label, const std::filesystem::path& path, const std::string& detail = {}) -> void;

}  // namespace icts::schema
