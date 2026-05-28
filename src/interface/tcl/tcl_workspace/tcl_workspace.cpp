// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the
// Mulan PSL v2.
// ***************************************************************************************

#include "tcl_workspace.h"

#include <string>

#include "workspace.h"

namespace tcl {

namespace {

constexpr const char* TCL_KEY = "-key";
constexpr const char* TCL_INDEX = "-index";
constexpr const char* TCL_FORCE = "-force";
constexpr const char* TCL_STATE = "-state";
constexpr const char* TCL_RUNTIME = "-runtime";
constexpr const char* TCL_PEAK_MEMORY = "-peak_memory";

void setResult(const std::string& result)
{
  auto* interp = ieda::ScriptEngine::getOrCreateInstance()->get_interp();
  Tcl_SetObjResult(interp, Tcl_NewStringObj(result.c_str(), static_cast<int>(result.size())));
}

unsigned setError(const std::string& message)
{
  setResult(message);
  return 0;
}

std::string optionString(TclCmd* cmd, const char* option_name)
{
  TclOption* option = cmd->getOptionOrArg(option_name);
  if (option == nullptr || !option->is_set_val() || option->getStringVal() == nullptr) {
    return "";
  }
  return option->getStringVal();
}

bool hasString(TclCmd* cmd, const char* option_name)
{
  TclOption* option = cmd->getOptionOrArg(option_name);
  return option != nullptr && option->is_set_val() && option->getStringVal() != nullptr;
}

ieda_workspace::WorkspaceManager& workspaceManager()
{
  return ieda_workspace::WorkspaceManager::getInstance();
}

}  // namespace

CmdWorkspaceLoad::CmdWorkspaceLoad(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_PATH, 1, nullptr));
}

unsigned CmdWorkspaceLoad::check()
{
  return hasString(this, TCL_PATH) ? 1 : setError("workspace_load requires -path");
}

unsigned CmdWorkspaceLoad::exec()
{
  if (!check()) {
    return 0;
  }

  const std::string path = optionString(this, TCL_PATH);
  if (!workspaceManager().load(path)) {
    return setError("failed to load workspace: " + path);
  }
  setResult(workspaceManager().getValue("workspace.dir"));
  return 1;
}

CmdWorkspacePrepare::CmdWorkspacePrepare(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclIntOption(TCL_FORCE, 0, 1));
}

unsigned CmdWorkspacePrepare::check()
{
  return 1;
}

unsigned CmdWorkspacePrepare::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* force_option = getOptionOrArg(TCL_FORCE);
  const bool force = force_option == nullptr ? true : force_option->getIntVal() != 0;
  if (!workspaceManager().prepare(force)) {
    return setError("failed to prepare workspace");
  }
  setResult(workspaceManager().getValue("workspace.dir"));
  return 1;
}

CmdWorkspaceGet::CmdWorkspaceGet(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_KEY, 1, nullptr));
}

unsigned CmdWorkspaceGet::check()
{
  return hasString(this, TCL_KEY) ? 1 : setError("workspace_get requires -key");
}

unsigned CmdWorkspaceGet::exec()
{
  if (!check()) {
    return 0;
  }

  setResult(workspaceManager().getValue(optionString(this, TCL_KEY)));
  return 1;
}

CmdWorkspaceStepGet::CmdWorkspaceStepGet(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_STEP, 1, nullptr));
  addOption(new TclStringOption(TCL_KEY, 1, nullptr));
}

unsigned CmdWorkspaceStepGet::check()
{
  if (!hasString(this, TCL_STEP)) {
    return setError("workspace_step_get requires -step");
  }
  if (!hasString(this, TCL_KEY)) {
    return setError("workspace_step_get requires -key");
  }
  return 1;
}

unsigned CmdWorkspaceStepGet::exec()
{
  if (!check()) {
    return 0;
  }

  setResult(workspaceManager().getStepValue(optionString(this, TCL_STEP), optionString(this, TCL_KEY)));
  return 1;
}

CmdWorkspaceStepName::CmdWorkspaceStepName(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclIntOption(TCL_INDEX, 1, -1));
}

unsigned CmdWorkspaceStepName::check()
{
  TclOption* index_option = getOptionOrArg(TCL_INDEX);
  if (index_option == nullptr || !index_option->is_set_val()) {
    return setError("workspace_step_name requires -index");
  }
  return 1;
}

unsigned CmdWorkspaceStepName::exec()
{
  if (!check()) {
    return 0;
  }

  const int index = getOptionOrArg(TCL_INDEX)->getIntVal();
  if (index < 0) {
    return setError("workspace_step_name index must be non-negative");
  }

  setResult(workspaceManager().getStepName(static_cast<size_t>(index)));
  return 1;
}

CmdWorkspaceStepCount::CmdWorkspaceStepCount(const char* cmd_name) : TclCmd(cmd_name)
{
}

unsigned CmdWorkspaceStepCount::check()
{
  return 1;
}

unsigned CmdWorkspaceStepCount::exec()
{
  setResult(std::to_string(workspaceManager().stepCount()));
  return 1;
}

CmdWorkspaceSetState::CmdWorkspaceSetState(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_STEP, 1, nullptr));
  addOption(new TclStringOption("-tool", 1, ""));
  addOption(new TclStringOption(TCL_STATE, 1, nullptr));
  addOption(new TclStringOption(TCL_RUNTIME, 1, ""));
  addOption(new TclIntOption(TCL_PEAK_MEMORY, 1, -1));
}

unsigned CmdWorkspaceSetState::check()
{
  if (!hasString(this, TCL_STEP)) {
    return setError("workspace_set_state requires -step");
  }
  if (!hasString(this, TCL_STATE)) {
    return setError("workspace_set_state requires -state");
  }
  return 1;
}

unsigned CmdWorkspaceSetState::exec()
{
  if (!check()) {
    return 0;
  }

  const std::string step = optionString(this, TCL_STEP);
  const std::string tool = optionString(this, "-tool");
  const std::string state = optionString(this, TCL_STATE);
  const std::string runtime = optionString(this, TCL_RUNTIME);
  const int peak_memory = getOptionOrArg(TCL_PEAK_MEMORY)->getIntVal();

  if (!workspaceManager().updateStepState(step, tool, state, runtime, peak_memory)) {
    return setError("failed to update workspace step state: " + step);
  }
  setResult(state);
  return 1;
}

}  // namespace tcl
