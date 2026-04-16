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
 * @file Schema.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-16
 * @brief Structured report schema writer for iCTS runtime and test artifacts.
 */

#include "Schema.hh"

#include <glog/logging.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "Log.hh"

namespace icts::schema {
namespace {

auto BuildGeneratedOnString() -> std::string
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t time_value = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &time_value);
#else
  localtime_r(&time_value, &local_tm);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return stream.str();
}

auto BuildRunHeader(const std::string& run_title) -> std::string
{
  std::ostringstream stream;
  stream << logformat::MakeTitle(run_title.empty() ? "iCTS Report" : run_title, '#', 79U) << '\n';
  stream << "Generate the report at " << BuildGeneratedOnString() << '\n';
  return stream.str();
}

auto FilterRunMetadata(const KeyValueFields& metadata) -> KeyValueFields
{
  KeyValueFields filtered_metadata;
  filtered_metadata.reserve(metadata.size());
  for (const auto& [key, value] : metadata) {
    if (key == "generated_on") {
      continue;
    }
    filtered_metadata.emplace_back(key, value);
  }
  return filtered_metadata;
}

auto BuildDetailBlock(const std::string& title, const std::vector<std::string>& lines) -> std::string
{
  std::ostringstream stream;
  stream << logformat::MakeTitle(title);
  if (lines.empty()) {
    stream << "\n(empty)";
    return stream.str();
  }
  stream << '\n';
  for (const auto& line : lines) {
    stream << line << '\n';
  }
  std::string block = stream.str();
  if (!block.empty() && block.back() == '\n') {
    block.pop_back();
  }
  return block;
}

auto BuildDiagnosticFields(DiagnosticLevel level, const std::string& owner, const std::string& summary, const KeyValueFields& fields)
    -> KeyValueFields
{
  KeyValueFields diagnostic_fields;
  diagnostic_fields.reserve(fields.size() + 3U);

  std::string severity = "info";
  switch (level) {
    case DiagnosticLevel::kInfo:
      severity = "info";
      break;
    case DiagnosticLevel::kWarning:
      severity = "warning";
      break;
    case DiagnosticLevel::kError:
      severity = "error";
      break;
    case DiagnosticLevel::kFallback:
      severity = "fallback";
      break;
  }

  diagnostic_fields.emplace_back("severity", severity);
  diagnostic_fields.emplace_back("owner", owner);
  diagnostic_fields.emplace_back("summary", summary);
  diagnostic_fields.insert(diagnostic_fields.end(), fields.begin(), fields.end());
  return diagnostic_fields;
}

auto FormatSeconds(std::chrono::steady_clock::duration duration) -> std::string
{
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  const double seconds = static_cast<double>(milliseconds) / 1000.0;

  std::ostringstream stream;
  stream.setf(std::ios::fixed, std::ios::floatfield);
  stream.precision(3);
  stream << seconds;
  return stream.str();
}

auto FlattenFields(const KeyValueFields& fields) -> std::string
{
  std::ostringstream stream;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0U) {
      stream << ", ";
    }
    stream << fields.at(index).first << "=" << fields.at(index).second;
  }
  return stream.str();
}

auto EnsureParentDir(const std::filesystem::path& path) -> bool
{
  std::error_code error_code;
  const auto parent = path.parent_path();
  if (parent.empty()) {
    return true;
  }
  std::filesystem::create_directories(parent, error_code);
  return !error_code;
}

auto AppendBlockToPath(const std::filesystem::path& path, const std::string& run_title, const std::string& block) -> void
{
  if (path.empty() || block.empty() || !EnsureParentDir(path)) {
    return;
  }

  std::error_code error_code;
  const bool file_exists = std::filesystem::exists(path, error_code) && !error_code;
  const auto size = file_exists ? std::filesystem::file_size(path, error_code) : 0U;
  const bool needs_header = !file_exists || error_code || size == 0U;

  std::ofstream stream(path, std::ios::out | std::ios::app);
  if (!stream.is_open()) {
    LOG_WARNING << "SchemaWriter: cannot open report file " << path.string();
    return;
  }

  if (needs_header) {
    stream << BuildRunHeader(run_title);
  }
  stream << '\n' << block;
  if (block.back() != '\n') {
    stream << '\n';
  }
  stream.flush();
}

}  // namespace

auto SchemaWriter::open(const std::filesystem::path& path, const std::string& run_title, const KeyValueFields& metadata) -> void
{
  if (path.empty()) {
    return;
  }

  const std::scoped_lock lock(_mutex);
  if (!EnsureParentDir(path)) {
    LOG_WARNING << "SchemaWriter: cannot create report directory for " << path.string();
    return;
  }

  std::ofstream next_stream(path, std::ios::out | std::ios::trunc);
  if (!next_stream.is_open()) {
    LOG_WARNING << "SchemaWriter: cannot open report file " << path.string();
    return;
  }

  next_stream << BuildRunHeader(run_title);
  const KeyValueFields filtered_metadata = FilterRunMetadata(metadata);
  if (!filtered_metadata.empty()) {
    next_stream << '\n' << logformat::MakeKeyValueTable("Run Context", filtered_metadata) << '\n';
  }
  next_stream.flush();

  if (_stream.is_open()) {
    _suspended_writers.push_back(SuspendedWriter{
        .path = _path,
        .has_content = _has_content,
    });
    _stream.flush();
    _stream.close();
  }

  _stream = std::move(next_stream);
  _path = path;
  _has_content = true;
}

auto SchemaWriter::close() -> void
{
  const std::scoped_lock lock(_mutex);
  if (_stream.is_open()) {
    _stream.flush();
    _stream.close();
  }
  restoreSuspendedWriterLocked();
}

auto SchemaWriter::restoreSuspendedWriterLocked() -> void
{
  if (_suspended_writers.empty()) {
    _path.clear();
    _has_content = false;
    return;
  }

  const auto suspended_writer = _suspended_writers.back();
  _suspended_writers.pop_back();

  std::ofstream restored_stream(suspended_writer.path, std::ios::out | std::ios::app);
  if (!restored_stream.is_open()) {
    LOG_WARNING << "SchemaWriter: cannot restore suspended report file " << suspended_writer.path.string();
    _path.clear();
    _has_content = false;
    return;
  }

  _stream = std::move(restored_stream);
  _path = suspended_writer.path;
  _has_content = suspended_writer.has_content;
}

auto SchemaWriter::isOpen() const -> bool
{
  const std::scoped_lock lock(_mutex);
  return _stream.is_open();
}

auto SchemaWriter::getActivePath() const -> std::filesystem::path
{
  const std::scoped_lock lock(_mutex);
  return _path;
}

auto SchemaWriter::writeBlockLocked(const std::string& block) -> void
{
  if (!_stream.is_open() || block.empty()) {
    return;
  }
  if (_has_content) {
    _stream << '\n';
  }
  _stream << block;
  if (block.back() != '\n') {
    _stream << '\n';
  }
  _stream.flush();
  _has_content = true;
}

auto SchemaWriter::emitSection(const std::string& title) -> void
{
  const std::scoped_lock lock(_mutex);
  writeBlockLocked(logformat::MakeTitle(title));
}

auto SchemaWriter::emitTable(const std::string& title, const std::vector<std::string>& headers, const TableRows& rows) -> void
{
  const std::scoped_lock lock(_mutex);
  writeBlockLocked(logformat::MakeTitledTable(title, headers, rows));
}

auto SchemaWriter::emitKeyValueTable(const std::string& title, const KeyValueFields& fields) -> void
{
  const std::scoped_lock lock(_mutex);
  writeBlockLocked(logformat::MakeKeyValueTable(title, fields));
}

auto SchemaWriter::emitDetailBlock(const std::string& title, const std::vector<std::string>& lines) -> void
{
  const std::scoped_lock lock(_mutex);
  writeBlockLocked(BuildDetailBlock(title, lines));
}

auto SchemaWriter::emitDiagnostic(DiagnosticLevel level, const std::string& owner, const std::string& summary, const KeyValueFields& fields)
    -> void
{
  emitKeyValueTable(owner + " Diagnostic", BuildDiagnosticFields(level, owner, summary, fields));
}

auto SchemaWriter::emitArtifact(const std::string& label, const std::filesystem::path& path, const std::string& detail) -> void
{
  KeyValueFields fields = {
      {"label", label},
      {"path", path.string()},
  };
  if (!detail.empty()) {
    fields.emplace_back("detail", detail);
  }
  emitKeyValueTable("Generated Artifact", fields);
}

auto SchemaWriter::appendStandaloneTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                         const std::vector<std::string>& headers, const TableRows& rows) -> void
{
  appendStandaloneBlock(path, run_title, logformat::MakeTitledTable(title, headers, rows));
}

auto SchemaWriter::appendStandaloneKeyValueTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                                 const KeyValueFields& fields) -> void
{
  appendStandaloneBlock(path, run_title, logformat::MakeKeyValueTable(title, fields));
}

auto SchemaWriter::appendStandaloneDetailBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                               const std::vector<std::string>& lines) -> void
{
  appendStandaloneBlock(path, run_title, BuildDetailBlock(title, lines));
}

auto SchemaWriter::appendStandaloneArtifact(const std::filesystem::path& path, const std::string& run_title, const std::string& label,
                                            const std::filesystem::path& artifact_path, const std::string& detail) -> void
{
  KeyValueFields fields = {
      {"label", label},
      {"path", artifact_path.string()},
  };
  if (!detail.empty()) {
    fields.emplace_back("detail", detail);
  }
  appendStandaloneKeyValueTable(path, run_title, "Generated Artifact", fields);
}

auto SchemaWriter::emitOrAppendKeyValueTable(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                             const KeyValueFields& fields) -> void
{
  auto& schema_writer = SCHEMA_WRITER_INST;
  const std::scoped_lock lock(schema_writer._mutex);
  const auto block = logformat::MakeKeyValueTable(title, fields);
  if (schema_writer._stream.is_open() && schema_writer._path == path) {
    schema_writer.writeBlockLocked(block);
    return;
  }
  AppendBlockToPath(path, run_title, block);
}

auto SchemaWriter::emitOrAppendDetailBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& title,
                                           const std::vector<std::string>& lines) -> void
{
  auto& schema_writer = SCHEMA_WRITER_INST;
  const std::scoped_lock lock(schema_writer._mutex);
  const auto block = BuildDetailBlock(title, lines);
  if (schema_writer._stream.is_open() && schema_writer._path == path) {
    schema_writer.writeBlockLocked(block);
    return;
  }
  AppendBlockToPath(path, run_title, block);
}

auto SchemaWriter::appendStandaloneBlock(const std::filesystem::path& path, const std::string& run_title, const std::string& block) -> void
{
  auto& schema_writer = SCHEMA_WRITER_INST;
  const std::scoped_lock lock(schema_writer._mutex);
  AppendBlockToPath(path, run_title, block);
}

ScopedStage::ScopedStage(std::string module, std::string stage, const KeyValueFields& start_fields)
    : _module(std::move(module)), _stage(std::move(stage)), _start_time(std::chrono::steady_clock::now())
{
  LOG_INFO << "";
  LOG_INFO << logformat::MakeStageMarker(_module, _stage, "START");
  if (!start_fields.empty()) {
    SCHEMA_WRITER_INST.emitKeyValueTable(_module + " " + _stage + " Context", start_fields);
  }
}

ScopedStage::~ScopedStage()
{
  if (!_finished) {
    finish({}, "success");
  }
}

auto ScopedStage::markRunning(const std::string& summary, const KeyValueFields& fields) -> void
{
  if (!summary.empty()) {
    LOG_INFO << logformat::MakeStageMarker(_module, summary, "RUNNING");
  } else {
    LOG_INFO << logformat::MakeStageMarker(_module, _stage, "RUNNING");
  }
  (void) fields;
}

auto ScopedStage::finish(const KeyValueFields& finish_fields, std::string outcome) -> void
{
  if (_finished) {
    return;
  }
  _finished = true;

  KeyValueFields summary_fields = finish_fields;
  summary_fields.emplace_back("outcome", std::move(outcome));
  summary_fields.emplace_back("elapsed_s", FormatSeconds(std::chrono::steady_clock::now() - _start_time));

  const std::string console_summary = FlattenFields(summary_fields);
  LOG_INFO << logformat::MakeStageMarker(_module, _stage, "FINISHED") << (console_summary.empty() ? std::string{} : ": " + console_summary);
  SCHEMA_WRITER_INST.emitKeyValueTable(_module + " " + _stage + " Summary", summary_fields);
}

auto ScopedStage::skip(const KeyValueFields& finish_fields, std::string outcome) -> void
{
  finish(finish_fields, std::move(outcome));
}

auto EmitTable(const std::string& title, const std::vector<std::string>& headers, const TableRows& rows) -> void
{
  LOG_INFO << "";
  LOG_INFO << logformat::MakeTitledTable(title, headers, rows);
  SCHEMA_WRITER_INST.emitTable(title, headers, rows);
}

auto EmitKeyValueTable(const std::string& title, const KeyValueFields& fields) -> void
{
  LOG_INFO << "";
  LOG_INFO << logformat::MakeKeyValueTable(title, fields);
  SCHEMA_WRITER_INST.emitKeyValueTable(title, fields);
}

auto EmitDiagnostic(DiagnosticLevel level, const std::string& owner, const std::string& summary, const KeyValueFields& fields) -> void
{
  switch (level) {
    case DiagnosticLevel::kInfo:
      LOG_INFO << owner << ": " << summary;
      break;
    case DiagnosticLevel::kWarning:
    case DiagnosticLevel::kFallback:
      LOG_WARNING << owner << ": " << summary;
      break;
    case DiagnosticLevel::kError:
      LOG_ERROR << owner << ": " << summary;
      break;
  }
  SCHEMA_WRITER_INST.emitDiagnostic(level, owner, summary, fields);
}

auto EmitArtifact(const std::string& label, const std::filesystem::path& path, const std::string& detail) -> void
{
  LOG_INFO << label << " saved: " << path.string();
  SCHEMA_WRITER_INST.emitArtifact(label, path, detail);
}

}  // namespace icts::schema
