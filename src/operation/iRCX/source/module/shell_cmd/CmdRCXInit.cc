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
#include <iostream>
#include <memory>

#include "RCX.hpp"
#include "rcxShellCmd.hh"
namespace ircx {

CmdRCXInit::CmdRCXInit(const char* cmd_name) : TclCmd(cmd_name) {
  auto thread_option = std::make_unique<TclIntOption>("-thread", 1, kDefaultThreadCount);
  addOption(thread_option.release());
}

unsigned CmdRCXInit::check() {
  return 1;
}

unsigned CmdRCXInit::exec() {
  if (!check()) {
    return 0;
  }

  const std::string hello_info =
      "\033[49;32m***************************\n"
      "  _  _____   _____ __   __\n"
      " (_)|  __ \\ /  __ \\\\ \\ / /\n"
      "  _ | |__) || |     \\ V / \n"
      " | ||  _  / | |      > <  \n"
      " | || | \\ \\ | |____ / . \\ \n"
      " |_||_|  \\_\\ \\_____/_/ \\_\\\n"
      "***************************\n"
      "WELCOME TO iRCX TCL-shell interface. \e[0m";
  std::cout << hello_info << std::endl;

  RCX& rcx = RCX::getOrCreateInst();

  TclOption* thread_option = getOptionOrArg("-thread");
  if (thread_option) {
    unsigned thread_count = thread_option->getIntVal();
    rcx.set_num_threads(thread_count);
  }

  return 1;
}

}  // namespace ircx
