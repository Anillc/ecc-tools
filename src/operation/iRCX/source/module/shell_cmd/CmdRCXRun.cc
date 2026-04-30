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
 * @file CmdRCXInit.cc
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-09
 */
#include <memory>
#include <omp.h>

#include "RCX.hpp"
#include "rcxShellCmd.hh"
namespace ircx {

CmdRCXRun::CmdRCXRun(const char* cmd_name) : TclCmd(cmd_name) {

}

unsigned CmdRCXRun::check() {
  return 1;
}

unsigned CmdRCXRun::exec() {
  unsigned ret = 1;

  if (!check()) {
    return 0;
  }

  RCX& rcx = RCX::getOrCreateInst();

  LOG_INFO << "RCX run begin...";
  omp_set_num_threads(rcx.num_threads());

  ret &= rcx.adaptDB();
  ret &= rcx.buildTopology();
  ret &= rcx.buildEnvironment();
  ret &= rcx.buildProcessVariation();
  // ret &= rcx.checkShortOpen();
  ret &= rcx.extractParasitics();
  LOG_INFO << "RCX run end.";
  
  return ret;
}

}  // namespace ircx
