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
 * @file Solver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <string>
#include <vector>

namespace icts {

class CtsPin;
class Net;
struct SolverRuntimeContext;

class Solver
{
 public:
  static Solver& getInst();

  Solver(const Solver&) = delete;
  Solver(Solver&&) = delete;
  Solver& operator=(const Solver&) = delete;
  Solver& operator=(Solver&&) = delete;

  std::vector<Net*> solve(const std::string& net_name, CtsPin* cts_driver, const std::vector<CtsPin*>& cts_pins) const;
  std::vector<Net*> solve(const std::string& net_name, CtsPin* cts_driver, const std::vector<CtsPin*>& cts_pins,
                          const SolverRuntimeContext& runtime_context) const;

 private:
  Solver() = default;
  ~Solver() = default;
};

}  // namespace icts
