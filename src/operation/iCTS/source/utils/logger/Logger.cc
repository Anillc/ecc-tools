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
 * @file Logger.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-15
 * @brief iCTS logger wrapper for file + console output.
 */
#include "Logger.hh"

#include <glog/logging.h>

#include <mutex>
#include <ostream>
#include <string>
#include <utility>

#include "log/Log.hh"

namespace icts {
namespace {

auto level_to_string(Logger::Level level) -> const char*
{
  switch (level) {
    case Logger::Level::kInfo:
      return "INFO";
    case Logger::Level::kWarning:
      return "WARNING";
    case Logger::Level::kError:
      return "ERROR";
    case Logger::Level::kFatal:
      return "FATAL";
  }
  return "INFO";
}

}  // namespace

Logger::Stream::Stream(Logger* logger, Level level, const char* file, int line, bool active)
    : _logger(logger), _level(level), _file(file), _line(line), _active(active)
{
}

Logger::Stream::Stream(Stream&& other) noexcept
    : _logger(other._logger),
      _level(other._level),
      _file(other._file),
      _line(other._line),
      _stream(std::move(other._stream)),
      _active(other._active)
{
  other._active = false;
}

auto Logger::Stream::operator=(Stream&& other) noexcept -> Logger::Stream&
{
  if (this != &other) {
    _logger = other._logger;
    _level = other._level;
    _file = other._file;
    _line = other._line;
    _stream = std::move(other._stream);
    _active = other._active;
    other._active = false;
  }
  return *this;
}

Logger::Stream::~Stream()
{
  if (!_active || _logger == nullptr) {
    return;
  }
  const auto message = _stream.str();
  _logger->write(_level, message);
  Logger::logToConsole(_level, _file, _line, message);
}

void Logger::set_log_file(const std::string& log_file)
{
  const std::scoped_lock lock(_mutex);
  if (_log_file == log_file && _ofs.is_open()) {
    return;
  }
  _log_file = log_file;
  if (_ofs.is_open()) {
    _ofs.close();
  }
  if (_log_file.empty()) {
    return;
  }
  _ofs.open(_log_file, std::ios::out | std::ios::trunc);
  if (!_ofs.is_open()) {
    LOG_WARNING << "CTS logger can't open log file: " << _log_file;
  }
}

void Logger::close()
{
  const std::scoped_lock lock(_mutex);
  if (_ofs.is_open()) {
    _ofs.close();
  }
}

void Logger::write(Level level, const std::string& message)
{
  const std::scoped_lock lock(_mutex);
  if (!_ofs.is_open()) {
    return;
  }
  _ofs << "[" << level_to_string(level) << "] " << message;
  if (message.empty() || message.back() != '\n') {
    _ofs << '\n';
  }
  _ofs.flush();
}

auto Logger::logToConsole(Level level, const char* file, int line, const std::string& message) -> void
{
  const bool has_site = file != nullptr;
  const char* prefix = has_site ? "[" : "";
  const char* suffix = has_site ? "] " : "";
  switch (level) {
    case Level::kInfo:
      if (has_site) {
        LOG_INFO << prefix << file << ":" << line << suffix << message;
      } else {
        LOG_INFO << message;
      }
      break;
    case Level::kWarning:
      if (has_site) {
        LOG_WARNING << prefix << file << ":" << line << suffix << message;
      } else {
        LOG_WARNING << message;
      }
      break;
    case Level::kError:
      if (has_site) {
        LOG_ERROR << prefix << file << ":" << line << suffix << message;
      } else {
        LOG_ERROR << message;
      }
      break;
    case Level::kFatal:
      if (has_site) {
        LOG_FATAL << prefix << file << ":" << line << suffix << message;
      } else {
        LOG_FATAL << message;
      }
      break;
  }
}

}  // namespace icts
