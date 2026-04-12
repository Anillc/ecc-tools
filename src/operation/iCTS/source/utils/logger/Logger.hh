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
 * @file Logger.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-15
 * @brief iCTS logger wrapper for file + console output.
 */

#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace icts {

#define LOG_INST (icts::Logger::getInst())

class Logger
{
 public:
  enum class Level
  {
    kInfo,
    kWarning,
    kError,
    kFatal
  };

  class Stream
  {
   public:
    Stream(Logger* logger, Level level, const char* file, int line, bool active = true);
    Stream(const Stream&) = delete;
    auto operator=(const Stream&) -> Stream& = delete;
    Stream(Stream&& other) noexcept;
    auto operator=(Stream&& other) noexcept -> Stream&;
    ~Stream();

    template <typename T>
    auto operator<<(const T& value) -> Stream&
    {
      if (!_active) {
        return *this;
      }
      _stream << value;
      return *this;
    }

    auto operator<<(std::ostream& (*manip)(std::ostream&) ) -> Stream&
    {
      if (!_active) {
        return *this;
      }
      manip(_stream);
      return *this;
    }

   private:
    Logger* _logger = nullptr;
    Level _level = Level::kInfo;
    const char* _file = nullptr;
    int _line = 0;
    std::ostringstream _stream;
    bool _active = true;
  };

  static auto getInst() -> Logger&
  {
    static Logger inst;
    return inst;
  }

  auto set_log_file(const std::string& log_file) -> void;
  auto close() -> void;

 private:
  Logger() = default;
  ~Logger() = default;

  auto write(Level level, const std::string& message) -> void;
  static auto logToConsole(Level level, const char* file, int line, const std::string& message) -> void;

  std::mutex _mutex;
  std::ofstream _ofs;
  std::string _log_file;
};

}  // namespace icts

#define CTS_LOG_INFO ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kInfo, __FILE__, __LINE__)
#define CTS_LOG_WARNING ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kWarning, __FILE__, __LINE__)
#define CTS_LOG_ERROR ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kError, __FILE__, __LINE__)
#define CTS_LOG_FATAL ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kFatal, __FILE__, __LINE__)

#define CTS_LOG_INFO_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kInfo, __FILE__, __LINE__, (condition))
#define CTS_LOG_WARNING_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kWarning, __FILE__, __LINE__, (condition))
#define CTS_LOG_ERROR_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kError, __FILE__, __LINE__, (condition))
#define CTS_LOG_FATAL_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kFatal, __FILE__, __LINE__, (condition))
