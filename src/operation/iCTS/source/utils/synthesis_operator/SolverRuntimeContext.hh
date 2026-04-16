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
 * @file SolverRuntimeContext.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace icts {

struct SolverRuntimeContext
{
  std::function<double()> min_buffering_length_provider;
  std::function<bool(const std::string&)> cell_lib_exist_provider;
  std::function<size_t()> gen_id_provider;
  std::function<void(const std::string&)> save_log;
  std::function<double(const std::string&)> cell_area_provider;
  std::function<double(const std::string&)> cell_leakage_power_provider;

  bool isReady() const
  {
    return min_buffering_length_provider != nullptr && cell_lib_exist_provider != nullptr && gen_id_provider != nullptr
           && save_log != nullptr && cell_area_provider != nullptr && cell_leakage_power_provider != nullptr;
  }

  double minBufferingLength() const { return min_buffering_length_provider(); }
  bool cellLibExist(const std::string& cell_master) const { return cell_lib_exist_provider(cell_master); }
  size_t genId() const { return gen_id_provider(); }
  void saveToLog(const std::string& msg) const { save_log(msg); }
  double cellArea(const std::string& cell_master) const { return cell_area_provider(cell_master); }
  double cellLeakagePower(const std::string& cell_master) const { return cell_leakage_power_provider(cell_master); }
};

SolverRuntimeContext CreateDefaultSolverRuntimeContext();

}  // namespace icts
