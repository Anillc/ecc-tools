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
#include "tcl_definition.h"

using ieda::TclCmd;
using ieda::TclIntOption;
using ieda::TclOption;
using ieda::TclStringOption;

namespace tcl {

class CmdWorkspaceLoad : public TclCmd
{
 public:
  explicit CmdWorkspaceLoad(const char* cmd_name);
  ~CmdWorkspaceLoad() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspacePrepare : public TclCmd
{
 public:
  explicit CmdWorkspacePrepare(const char* cmd_name);
  ~CmdWorkspacePrepare() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspaceGet : public TclCmd
{
 public:
  explicit CmdWorkspaceGet(const char* cmd_name);
  ~CmdWorkspaceGet() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspaceStepGet : public TclCmd
{
 public:
  explicit CmdWorkspaceStepGet(const char* cmd_name);
  ~CmdWorkspaceStepGet() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspaceStepName : public TclCmd
{
 public:
  explicit CmdWorkspaceStepName(const char* cmd_name);
  ~CmdWorkspaceStepName() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspaceStepCount : public TclCmd
{
 public:
  explicit CmdWorkspaceStepCount(const char* cmd_name);
  ~CmdWorkspaceStepCount() override = default;

  unsigned check() override;
  unsigned exec() override;
};

class CmdWorkspaceSetState : public TclCmd
{
 public:
  explicit CmdWorkspaceSetState(const char* cmd_name);
  ~CmdWorkspaceSetState() override = default;

  unsigned check() override;
  unsigned exec() override;
};

}  // namespace tcl
