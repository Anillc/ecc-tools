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
#pragma once
/**
 * @File Name: tcl_register_rcx.h
 * @Brief :
 * @Author : Yipei Xu (yipeix@163.com)
 * @Version : 1.0
 * @Creat Date : 2025-12-08
 *
 */
#include "ScriptEngine.hh"
#include "UserShell.hh"

#define TCL_USERSHELL

#ifdef TCL_USERSHELL
#include "rcxShellCmd.hh"
#endif

using namespace ieda;

namespace tcl {
int registerCmdRCX()
{
  registerTclCmd(ircx::CmdRCXRun, "run_rcx");
  registerTclCmd(ircx::CmdRCXInit, "init_rcx");
  registerTclCmd(ircx::CmdRCXReport, "report_rcx");

  registerTclCmd(ircx::CmdReadCorner, "read_corner");
  registerTclCmd(ircx::CmdReadMapping, "read_mapping");

  return EXIT_SUCCESS;
}

}  // namespace tcl
