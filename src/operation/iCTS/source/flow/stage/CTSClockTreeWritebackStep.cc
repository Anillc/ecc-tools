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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CTSClockTreeWritebackStep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-tree writeback step boundary implementation.
 */

#include "stage/CTSClockTreeWritebackStep.hh"

#include <string>

#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"

namespace icts {

auto CTSClockTreeWritebackStep::run() -> CTSClockTreeWritebackResult
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("writeback");
  auto writeback_stage = SCHEMA_WRITER_INST.beginStage("CTSWriteback", "Write synthesized CTS topology back to iDB");
  SCHEMA_WRITER_INST.emitSection("### iDB Writeback");

  auto clocks = DESIGN_INST.get_clocks();
  CTSClockTreeWritebackResult result{
      .attempted = true,
      .design_ready = WRAPPER_INST.is_design_ready(),
      .writeback_done = false,
      .clock_count = clocks.size(),
  };

  if (result.design_ready) {
    result.writeback_done = WRAPPER_INST.writeClocks(clocks);
  }

  const std::string status = result.writeback_done ? "finished" : "failed";
  schema::EmitKeyValueTable("CTS Writeback Summary", {
                                                         {"semantic_owner", "synthesis_flow"},
                                                         {"status", status},
                                                         {"design_ready", result.design_ready ? "true" : "false"},
                                                         {"clock_count", std::to_string(result.clock_count)},
                                                     });

  if (result.writeback_done) {
    (void) runtime.finished();
    writeback_stage.finished({{"clock_count", std::to_string(result.clock_count)}});
  } else {
    (void) runtime.failed();
    writeback_stage.failed({{"clock_count", std::to_string(result.clock_count)}});
  }
  return result;
}

}  // namespace icts
