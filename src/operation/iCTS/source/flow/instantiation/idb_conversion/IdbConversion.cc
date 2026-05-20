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
 * @file IdbConversion.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS iDB conversion entry implementation.
 */

#include "instantiation/idb_conversion/IdbConversion.hh"

#include <string>
#include <utility>
#include <vector>

#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"

namespace icts {

auto IdbConversion::run() -> IdbConversionResult
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("instantiation");
  auto instantiation_stage = SCHEMA_WRITER_INST.beginStage("Instantiation", "Instantiate synthesized CTS topology into iDB", {},
                                                           schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("### iDB Conversion");

  auto clocks = DESIGN_INST.get_clocks();
  IdbConversionResult result{
      .attempted = true,
      .design_ready = WRAPPER_INST.is_design_ready(),
      .idb_conversion_done = false,
      .clock_count = clocks.size(),
  };

  WrapperWriteResult write_result;
  if (result.design_ready) {
    write_result = WRAPPER_INST.writeClocksDetailed(clocks);
    result.idb_conversion_done = write_result.success;
  }

  const std::string status = result.idb_conversion_done ? "finished" : "failed";
  schema::KeyValueFields overview_fields = {
      {"semantic_owner", "instantiation"},
      {"status", status},
      {"design_ready", result.design_ready ? "true" : "false"},
      {"clock_count", std::to_string(result.clock_count)},
  };
  if (!result.idb_conversion_done) {
    overview_fields.emplace_back("failed_clock", write_result.failed_clock.empty() ? "n/a" : write_result.failed_clock);
    overview_fields.emplace_back("failed_net", write_result.failed_net.empty() ? "n/a" : write_result.failed_net);
    overview_fields.emplace_back("idb_clock_tree_restored", write_result.idb_clock_tree_restored ? "true" : "false");
    overview_fields.emplace_back("failure_reason", write_result.reason.empty() ? "n/a" : write_result.reason);
  }
  schema::EmitKeyValueTable("CTS Instantiation Overview", overview_fields);

  if (result.idb_conversion_done) {
    (void) runtime.finished();
    instantiation_stage.finished({{"clock_count", std::to_string(result.clock_count)}});
  } else {
    (void) runtime.failed();
    instantiation_stage.failed({{"clock_count", std::to_string(result.clock_count)}});
  }
  return result;
}

}  // namespace icts
