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
 * @file Evaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS evaluation entry facade implementation.
 */

#include "evaluation/Evaluation.hh"

#include <string>

#include "logger/Schema.hh"

namespace icts {

auto Evaluation::run(EvaluationState& evaluation_state, bool refresh_sta_timing) -> EvaluationResult
{
  return run(evaluation_state, EvaluationOptions{.refresh_sta_timing = refresh_sta_timing});
}

auto Evaluation::run(EvaluationState& evaluation_state, const EvaluationOptions& options) -> EvaluationResult
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("evaluation");
  auto evaluation_stage = SCHEMA_WRITER_INST.beginStage("Evaluation", "Evaluate CTS clock tree", {},
                                                        schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Evaluation Overview");
  SCHEMA_WRITER_INST.emitSection("### Final Evaluation");
  evaluate(evaluation_state, options);
  const bool evaluation_ready = hasEvaluationResult(evaluation_state);
  if (evaluation_ready) {
    (void) runtime.finished();
    evaluation_stage.finished();
  } else {
    (void) runtime.failed();
    evaluation_stage.failed();
  }
  return EvaluationResult{.evaluation_ready = evaluation_ready};
}

auto Evaluation::evaluate(EvaluationState& state, const EvaluationOptions& options) -> void
{
  QorEvaluation::evaluate(state, options);
}

auto Evaluation::outputSummary(const EvaluationState& state) -> QorSummary
{
  return QorEvaluation::outputSummary(state);
}

auto Evaluation::hasEvaluationResult(const EvaluationState& state) -> bool
{
  return QorEvaluation::hasEvaluationResult(state);
}

auto Evaluation::reset(EvaluationState& state) -> void
{
  QorEvaluation::reset(state);
}

}  // namespace icts
