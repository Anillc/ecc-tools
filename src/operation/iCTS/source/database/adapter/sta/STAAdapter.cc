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

#include <glog/logging.h>

#include <cmath>
#include <ostream>
#include <string>

#include "Log.hh"
#include "STAAdapterInternal.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "sta/Sta.hh"

namespace icts {

STAAdapter::~STAAdapter() = default;

STAAdapter::CharPowerState::~CharPowerState() = default;

auto STAAdapter::init() -> void
{
  auto& adapter = getInst();
  adapter.resetCharTimingState();
  adapter.resetCharPowerState();
  adapter._is_char_only_active = false;
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  if (!sta_adapter_internal::HasStaBaseContext()) {
    ipower::Power::destroyPower();
    ista::TimingEngine::destroyTimingEngine();
    timing_engine = sta_adapter_internal::GetStaEngine();
    sta_adapter_internal::InstallTimingIDBAdapter(timing_engine);
    sta_adapter_internal::LoadConfiguredLiberty(timing_engine);
  }

  timing_engine->set_num_threads(sta_adapter_internal::kStaThreadCount);
  sta_adapter_internal::ConfigureStaWorkspace(timing_engine, "sta");
  adapter.resetStaTransientState();
  timing_engine->get_ista()->set_n_worst_path_per_clock(sta_adapter_internal::kWorstPathPerClock);
}

auto STAAdapter::initCharOnly() -> void
{
  auto& adapter = getInst();
  if (adapter._is_char_only_active) {
    LOG_WARNING << "initCharOnly called while char-only mode is already active; "
                << "resetting the previous characterization state first.";
    finishCharOnly();
  }

  if (!sta_adapter_internal::HasStaBaseContext()) {
    init();
  }

  adapter._is_char_only_active = true;
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  timing_engine->set_num_threads(sta_adapter_internal::kCharStaThreadCount);
  sta_adapter_internal::ConfigureStaWorkspace(timing_engine, "sta_char");
  adapter.resetStaTransientState();
}

}  // namespace icts
