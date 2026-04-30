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
 * @file CTSClockDataLoadStep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-data load step orchestration implementation.
 */

#include "stage/CTSClockDataLoadStep.hh"

#include "logger/Schema.hh"
#include "netlist/ClockNetEditor.hh"

namespace icts {

auto CTSClockDataLoadStep::run() -> void
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("read_data");
  auto read_stage = SCHEMA_WRITER_INST.beginStage("CTSReadData", "Read CTS clock data");
  SCHEMA_WRITER_INST.emitSection("## Input Summary");
  SCHEMA_WRITER_INST.emitSection("### Clock Data");
  ClockNetEditor::readClockData();
  (void) runtime.finished();
  read_stage.finished();
}

}  // namespace icts
