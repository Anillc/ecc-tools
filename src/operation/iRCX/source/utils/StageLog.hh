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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "log/Log.hh"
#include "usage/usage.hh"

namespace ircx {

class StageLog
{
 public:
  explicit StageLog(std::string stage) : stage_(std::move(stage)) { LOG_INFO << stage_ << " begin."; }
  ~StageLog() { LOG_INFO << stage_ << " end: " << (success_ ? "success" : "failed") << "."; }

  StageLog(const StageLog&) = delete;
  StageLog& operator=(const StageLog&) = delete;

  void set_success(bool success = true) { success_ = success; }

 private:
  std::string stage_;
  bool success_{false};
};

struct StageLogOptions
{
  bool profile{false};
};

template <typename Func>
auto runStage(std::string stage, Func&& func, StageLogOptions options = {}) -> bool
{
  std::optional<ieda::Stats> stats;
  if (options.profile) {
    stats.emplace();
  }

  StageLog stage_log(stage);
  const bool success = std::forward<Func>(func)();
  stage_log.set_success(success);
  if (stats.has_value()) {
    LOG_INFO << "  - memory usage: " << stats->memoryDelta() << "MB";
    LOG_INFO << "  - time elapsed: " << stats->elapsedRunTime() << "s";
  }
  return success;
}

}  // namespace ircx
