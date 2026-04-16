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
 * @file Solver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "Solver.hh"

#include <utility>

#include "CtsConfig.hh"
#include "CtsRuntime.hh"
#include "NetSynthesisPipeline.hh"
#include "SolverPipelineData.hh"
#include "SolverRuntimeContext.hh"
#include "log/Log.hh"

namespace icts {

SolverRuntimeContext CreateDefaultSolverRuntimeContext()
{
  SolverRuntimeContext runtime;
  runtime.min_buffering_length_provider
      = []() -> double { return static_cast<double>(GetCtsRuntime().getConfig()->get_min_buffering_length()); };
  runtime.cell_lib_exist_provider = [](const std::string& cell_master) { return GetCtsRuntime().cellLibExist(cell_master); };
  runtime.gen_id_provider = []() { return GetCtsRuntime().genId(); };
  runtime.save_log = [](const std::string& msg) { GetCtsRuntime().saveToLog(msg); };
  runtime.cell_area_provider = [](const std::string& cell_master) { return GetCtsRuntime().getCellArea(cell_master); };
  runtime.cell_leakage_power_provider = [](const std::string& cell_master) { return GetCtsRuntime().getCellLeakagePower(cell_master); };
  return runtime;
}

Solver& Solver::getInst()
{
  static Solver instance;
  return instance;
}

std::vector<Net*> Solver::solve(const std::string& net_name, CtsPin* cts_driver, const std::vector<CtsPin*>& cts_pins) const
{
  return solve(net_name, cts_driver, cts_pins, CreateDefaultSolverRuntimeContext());
}

std::vector<Net*> Solver::solve(const std::string& net_name, CtsPin* cts_driver, const std::vector<CtsPin*>& cts_pins,
                                const SolverRuntimeContext& runtime_context) const
{
  LOG_FATAL_IF(!runtime_context.isReady()) << "Solver runtime context is not fully configured.";

  SolverPipelineState state;
  state.net_name = net_name;
  state.cts_driver = cts_driver;
  state.cts_pins = cts_pins;
  NetSynthesisPipeline pipeline(runtime_context);
  pipeline.run(state);
  return state.nets;
}

}  // namespace icts
