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
 * @file CmdReadMapping.cc
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-08
 */
#include "rcxShellCmd.hh"
#include "RCX.hpp"
#include "log/Log.hh"

#include <memory>
#include <filesystem>

namespace ircx {

CmdReadMapping::CmdReadMapping(const char* cmd_name) : TclCmd(cmd_name) {
  auto file_name_option = std::make_unique<TclStringOption>("file_name", 1, nullptr);
  addOption(file_name_option.release());
}

unsigned CmdReadMapping::check() {
  TclOption* file_name_option = getOptionOrArg("file_name");
  LOG_FATAL_IF(!file_name_option);
  return 1;
}

unsigned CmdReadMapping::exec() {
  if (!check()) {
    return 0;
  }

  TclOption* file_name_option = getOptionOrArg("file_name");
  char* mapping_file = file_name_option->getStringVal();
  LOG_FATAL_IF(!std::filesystem::exists(mapping_file)) <<
                            "mapping file not found: " << mapping_file;

  RCX& rcx = RCX::getOrCreateInst();
  return rcx.readMapping(mapping_file);
}

}  // namespace ircx
