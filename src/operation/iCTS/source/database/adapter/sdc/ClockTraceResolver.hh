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
 * @file ClockTraceResolver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief Resolves SDC clock roots into CTS synthesis target nets.
 */

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace idb {
class IdbDesign;
}  // namespace idb

namespace icts {

struct SdcClockData;

struct ClockTraceRecord
{
  std::string clock_name;
  std::string net_name;
  std::string status;
  std::string target_kind;
  std::size_t sequential_clock_sinks = 0U;
  std::size_t macro_clock_sinks = 0U;
  std::string trace_path;
  std::string reason;
  std::string clock_kind = "unknown";
  std::string master_clock_name = "n/a";
  std::string dominance = "undetermined";
};

struct ClockTraceResult
{
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  std::vector<ClockTraceRecord> records;
  std::vector<ClockTraceRecord> unowned_clock_like_records;
};

class ClockTraceResolver
{
 public:
  static auto resolve(const SdcClockData& clock_data) -> ClockTraceResult;
  static auto resolve(const SdcClockData& clock_data, idb::IdbDesign* idb_design) -> ClockTraceResult;
};

}  // namespace icts
