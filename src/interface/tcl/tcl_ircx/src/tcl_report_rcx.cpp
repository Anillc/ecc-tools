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
 * @file tcl_report_rcx.cpp
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-09
 */
#include "RCXAPI.hh"
#include "tcl_ircx.h"

namespace tcl {

TclReportRCX::TclReportRCX(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption("file_name", 1, nullptr));
  addOption(new TclSwitchOption("-geometry"));
}

unsigned TclReportRCX::check()
{
  TclOption* file_name_option = getOptionOrArg("file_name");
  LOG_FATAL_IF(!file_name_option);
  LOG_FATAL_IF(!file_name_option->is_set_val());
  return 1;
}

unsigned TclReportRCX::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* file_name_option = getOptionOrArg("file_name");
  const char* output_dir = file_name_option->getStringVal();

  return RCX_API_INST.reportSpef(output_dir ? output_dir : ".");
}

}  // namespace tcl
