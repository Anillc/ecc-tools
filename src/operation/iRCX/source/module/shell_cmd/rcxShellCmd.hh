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
 * @file SellCmd.hh
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-08
 */
#pragma once

#include "ScriptEngine.hh"
namespace ircx {

using ieda::ScriptEngine;
using ieda::TclCmd;
using ieda::TclCmds;
using ieda::TclDoubleListOption;
using ieda::TclDoubleOption;
using ieda::TclEncodeResult;
using ieda::TclIntListOption;
using ieda::TclIntOption;
using ieda::TclOption;
using ieda::TclStringListListOption;
using ieda::TclStringListOption;
using ieda::TclStringOption;
using ieda::TclSwitchOption;

/**
 * @brief Initialize RCX.
 *
 */
class CmdRCXInit : public TclCmd {
 public:
  explicit CmdRCXInit(const char* cmd_name);
  ~CmdRCXInit() override = default;

  unsigned check();
  unsigned exec();
};

/**
 * @brief Run RCX.
 *
 */
class CmdRCXRun : public TclCmd {
 public:
  explicit CmdRCXRun(const char* cmd_name);
  ~CmdRCXRun() override = default;

  unsigned check();
  unsigned exec();
};

/**
 * @brief Report RCX.
 *
 */
class CmdRCXReport : public TclCmd {
 public:
  explicit CmdRCXReport(const char* cmd_name);
  ~CmdRCXReport() override = default;

  unsigned check();
  unsigned exec();
};

/**
 * @brief Read one corner binding.
 *
 */
class CmdReadCorner : public TclCmd {
 public:
  explicit CmdReadCorner(const char* cmd_name);
  ~CmdReadCorner() override = default;

  unsigned check();
  unsigned exec();
};

/**
 * @brief Read mapping file.
 *
 */
class CmdReadMapping : public TclCmd {
 public:
  explicit CmdReadMapping(const char* cmd_name);
  ~CmdReadMapping() override = default;

  unsigned check();
  unsigned exec();
};

}  // namespace ircx
