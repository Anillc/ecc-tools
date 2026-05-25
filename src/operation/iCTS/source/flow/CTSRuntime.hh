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
 * @file CTSRuntime.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-24
 * @brief Explicit owner for iCTS runtime state at the API and flow boundary.
 */

#pragma once

#include "database/adapter/fast_sta/FastSta.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "utils/logger/Schema.hh"
#include "utils/logger/SchemaForward.hh"

namespace icts {

struct CTSRuntime
{
  Config config;
  Design design;
  Wrapper wrapper;
  STAAdapter sta_adapter;
  FastSTA fast_sta;
  SchemaWriter reporter;

  auto reset() -> void
  {
    config.reset();
    design.reset();
    wrapper.reset();
    fast_sta.reset();
    reporter.reset();
  }
};

}  // namespace icts
