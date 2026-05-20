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
 * @file tcl_read_corner.cpp
 * @author Yipei Xu (yipeix@163.com)
 * @brief Tcl command wrapper for binding one ITF with one captab.
 * @version 0.1
 * @date 2025-12-08
 */
#include <filesystem>

#include "RCX.hpp"
#include "log/Log.hh"
#include "tcl_ircx.h"
#include "tcl_util.h"

namespace tcl {

TclReadCorner::TclReadCorner(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.push_back(std::make_pair("-name", ValueType::kString));
  _config_list.push_back(std::make_pair("-itf", ValueType::kString));
  _config_list.push_back(std::make_pair("-captab", ValueType::kString));

  TclUtil::addOption(this, _config_list);
}

unsigned TclReadCorner::check()
{
  LOG_FATAL_IF(!getOptionOrArg("-name"));
  LOG_FATAL_IF(!getOptionOrArg("-itf"));
  LOG_FATAL_IF(!getOptionOrArg("-captab"));
  LOG_FATAL_IF(!getOptionOrArg("-name")->is_set_val());
  LOG_FATAL_IF(!getOptionOrArg("-itf")->is_set_val());
  LOG_FATAL_IF(!getOptionOrArg("-captab")->is_set_val());
  return 1;
}

unsigned TclReadCorner::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* name_option = getOptionOrArg("-name");
  TclOption* itf_option = getOptionOrArg("-itf");
  TclOption* captab_option = getOptionOrArg("-captab");

  char* corner_name = name_option->getStringVal();
  char* itf_file = itf_option->getStringVal();
  char* captab_file = captab_option->getStringVal();

  LOG_FATAL_IF(corner_name == nullptr || corner_name[0] == '\0')
      << "corner name is empty.";
  LOG_FATAL_IF(itf_file == nullptr || itf_file[0] == '\0')
      << "itf file is empty.";
  LOG_FATAL_IF(captab_file == nullptr || captab_file[0] == '\0')
      << "captab file is empty.";
  LOG_FATAL_IF(!std::filesystem::exists(itf_file))
      << "itf file not found: " << itf_file;
  LOG_FATAL_IF(!std::filesystem::exists(captab_file))
      << "captab file not found: " << captab_file;

  ircx::RCX& rcx = ircx::RCX::getOrCreateInst();
  return rcx.readCorner(corner_name, itf_file, captab_file);
}

}  // namespace tcl
