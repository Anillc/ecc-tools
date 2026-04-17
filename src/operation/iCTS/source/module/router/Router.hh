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
 * @file Router.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

namespace icts {
class CtsClock;
class CtsNet;
class CtsPin;
struct CTSContext;
class Router
{
 public:
  static Router& getInst();

  Router(const Router&) = delete;
  Router(Router&&) = delete;
  Router& operator=(const Router&) = delete;
  Router& operator=(Router&&) = delete;

  void run(const CTSContext& context);
  void reset();

 private:
  Router() = default;
  ~Router() = default;
};

}  // namespace icts
