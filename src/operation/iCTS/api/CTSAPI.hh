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
 * @file CTSAPI.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API
 */
#pragma once

#include <cstdint>
#include <string>

namespace ieda_feature {
struct CTSSummary;
}  // namespace ieda_feature

namespace icts {

#define CTSAPIInst (icts::CTSAPI::getInst())

class CTSAPI
{
 public:
  static CTSAPI& getInst()
  {
    static CTSAPI inst;
    return inst;
  }

  // CTS CLI
  void runCTS();
  void report(const std::string& save_dir);

  // Flow API
  void resetAPI();
  void init(const std::string& config_file, const std::string& work_dir = "");
  void readData();
  void summaryClockDistribution();

  // Feature API
  ieda_feature::CTSSummary outputSummary();

 private:
  CTSAPI() = default;
  CTSAPI(const CTSAPI& other) = delete;
  CTSAPI(CTSAPI&& other) = delete;
  ~CTSAPI() = default;
  CTSAPI& operator=(const CTSAPI& other) = delete;
  CTSAPI& operator=(CTSAPI&& other) = delete;
};

}  // namespace icts
