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
 * @file STAAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter initialization and reset implementation.
 */

#include "STAAdapter.hh"

#include <string>

#include "Vector.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "sta/Sta.hh"
#include "timing_query/STAAdapterTimingQuery.hh"

namespace icts {

auto STAAdapter::observeQueryFacade() const -> void
{
  // Query methods are intentionally instance calls even when the backing iSTA
  // storage is process-global: the runtime-owned facade is the dependency
  // boundary that future parallelization must replace or shard.
  (void) _base_context_ready;
}

auto STAAdapter::init(const Config& config) -> void
{
  auto* timing_engine = sta_adapter_timing_query::GetStaEngine();
  const bool has_loaded_liberty = timing_engine->get_ista() != nullptr && !timing_engine->get_ista()->getAllLib().empty();
  if (!has_loaded_liberty) {
    ipower::Power::destroyPower();
    ista::TimingEngine::destroyTimingEngine();
    timing_engine = sta_adapter_timing_query::GetStaEngine();
    sta_adapter_timing_query::LoadConfiguredLiberty(timing_engine);
  }

  // The global TimingEngine can outlive iDB reloads between flow steps.
  // Always rebind the adapter to the current dmInst builder before CTS queries RC.
  sta_adapter_timing_query::InstallTimingIDBAdapter(timing_engine);
  _base_context_ready = true;
  timing_engine->set_num_threads(sta_adapter_timing_query::kStaThreadCount);
  sta_adapter_timing_query::ConfigureStaWorkspace(config, timing_engine, "sta");
  resetStaTransientState();
  timing_engine->get_ista()->set_n_worst_path_per_clock(sta_adapter_timing_query::kWorstPathPerClock);
}

}  // namespace icts
