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
 * @file CTSFlow.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <string>

namespace ieda_feature {
class CTSSummary;
}  // namespace ieda_feature

namespace icts {

class CTSAPI;
struct CTSContext;

class CTSFlowRunner
{
 public:
  static void init(CTSAPI& api, const std::string& config_file, const std::string& work_dir);
  static void run(CTSAPI& api);
  static void report(CTSAPI& api, const std::string& save_dir);
  static void initEvalInfo(CTSAPI& api);
  static ieda_feature::CTSSummary outputSummary(CTSAPI& api);

 private:
  static void readData(CTSAPI& api, const CTSContext& context);
  static void routing(CTSAPI& api, const CTSContext& context);
  static void evaluate(CTSAPI& api, const CTSContext& context);
  static void resetSession(CTSAPI& api);
  static void applyWorkDirOverride(CTSAPI& api, const std::string& work_dir);
  static void ensureOutputDirs(const CTSAPI& api);
  static void ensureTimingEngine(CTSAPI& api);
  static bool ensureEvaluatorReady(CTSAPI& api, bool run_evaluate);
};

}  // namespace icts
