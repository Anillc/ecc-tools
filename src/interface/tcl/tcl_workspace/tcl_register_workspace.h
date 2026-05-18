// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the
// Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include "ScriptEngine.hh"
#include "UserShell.hh"
#include "tcl_workspace.h"

using namespace ieda;

namespace tcl {

int registerCmdWorkspace()
{
  registerTclCmd(CmdWorkspaceLoad, "workspace_load");
  registerTclCmd(CmdWorkspacePrepare, "workspace_prepare");
  registerTclCmd(CmdWorkspaceGet, "workspace_get");
  registerTclCmd(CmdWorkspaceStepGet, "workspace_step_get");
  registerTclCmd(CmdWorkspaceStepName, "workspace_step_name");
  registerTclCmd(CmdWorkspaceStepCount, "workspace_step_count");
  registerTclCmd(CmdWorkspaceSetState, "workspace_set_state");
  return EXIT_SUCCESS;
}

}  // namespace tcl
