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
 * @file CTSClockTreeEvaluationStep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-tree evaluation step orchestration implementation.
 */

#include "stage/CTSClockTreeEvaluationStep.hh"

#include "evaluation/ClockTreeEvaluator.hh"
#include "logger/Schema.hh"

namespace icts {

auto CTSClockTreeEvaluationStep::run(ClockTreeEvaluationState& evaluation_state, bool refresh_sta_timing) -> CTSClockTreeEvaluationResult
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("evaluation");
  auto evaluation_stage = SCHEMA_WRITER_INST.beginStage("CTSEvaluation", "Evaluate CTS clock tree");
  SCHEMA_WRITER_INST.emitSection("## Evaluation Summary");
  SCHEMA_WRITER_INST.emitSection("### Final Evaluation");
  ClockTreeEvaluator::evaluate(evaluation_state, ClockTreeEvaluationOptions{.refresh_sta_timing = refresh_sta_timing});
  const bool evaluation_ready = ClockTreeEvaluator::hasEvaluationResult(evaluation_state);
  if (evaluation_ready) {
    (void) runtime.finished();
    evaluation_stage.finished();
  } else {
    (void) runtime.failed();
    evaluation_stage.failed();
  }
  return CTSClockTreeEvaluationResult{.evaluation_ready = evaluation_ready};
}

}  // namespace icts
