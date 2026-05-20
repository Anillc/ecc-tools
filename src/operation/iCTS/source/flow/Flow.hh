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
 * @file Flow.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS flow lifecycle owner.
 */

#pragma once

#include <string>

#include "design/ClockLayout.hh"
#include "evaluation/Evaluation.hh"
#include "evaluation/qor/QorEvaluation.hh"
#include "instantiation/Instantiation.hh"
#include "optimization/Optimization.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/trace/SynthesisTrace.hh"

namespace icts {

#define FLOW_INST (icts::Flow::getInst())

enum class FlowStageStatus
{
  kFinished,
  kSkipped,
  kFailed
};

struct ClockDataReadResult
{
  FlowStageStatus status = FlowStageStatus::kFailed;
  std::string reason;
  bool clock_data_ready = false;
};

class Flow
{
 public:
  static auto getInst() -> Flow&
  {
    static Flow inst;
    return inst;
  }

  auto runCTS() -> void;
  auto readClockData() -> ClockDataReadResult;
  auto runSynthesis() -> SynthesisTraceSummary;
  auto runOptimization() -> OptimizationResult;
  auto evaluateClockTree() -> EvaluationResult;
  auto emitReports(const std::string& save_dir) -> void;
  auto outputRuntimeSetup() -> void;
  auto outputSummary() const -> QorSummary;
  auto outputRunSummary() const -> SynthesisTraceSummary;
  auto setSetupReady(bool setup_ready) -> void { _setup_ready = setup_ready; }
  auto reset() -> void;

  Flow(const Flow& other) = delete;
  Flow(Flow&& other) = delete;
  auto operator=(const Flow& other) -> Flow& = delete;
  auto operator=(Flow&& other) -> Flow& = delete;

 private:
  Flow() = default;
  ~Flow() = default;

  auto instantiateClockTree() -> InstantiationResult;
  auto emitKeyResults(double elapsed_time_s, double peak_vmem_delta_mb) const -> void;

  SynthesisTraceSummary _run_summary;
  ClockLayout _clock_layout;
  EvaluationState _evaluation_state;
  InstantiationResult _instantiation_result;
  CharacterizationLibrary _char_library;
  bool _runtime_setup_emitted = false;
  bool _setup_ready = false;
  bool _evaluation_ready = false;
};

}  // namespace icts
