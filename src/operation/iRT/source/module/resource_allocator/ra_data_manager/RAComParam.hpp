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
#pragma once

namespace irt {

class RAComParam
{
 public:
  RAComParam() = default;
  RAComParam(double supply_unit, double initial_penalty, double penalty_drop_rate, int32_t outer_iter_num, int32_t inner_iter_num)
  {
    _supply_unit = supply_unit;
    _initial_penalty = initial_penalty;
    _penalty_drop_rate = penalty_drop_rate;
    _outer_iter_num = outer_iter_num;
    _inner_iter_num = inner_iter_num;
  }
  ~RAComParam() = default;
  // getter
  double get_supply_unit() const { return _supply_unit; }
  double get_initial_penalty() const { return _initial_penalty; }
  double get_penalty_drop_rate() const { return _penalty_drop_rate; }
  int32_t get_outer_iter_num() const { return _outer_iter_num; }
  int32_t get_inner_iter_num() const { return _inner_iter_num; }
  // setter
  void set_supply_unit(const double supply_unit) { _supply_unit = supply_unit; }
  void set_initial_penalty(const double initial_penalty) { _initial_penalty = initial_penalty; }
  void set_penalty_drop_rate(const double penalty_drop_rate) { _penalty_drop_rate = penalty_drop_rate; }
  void set_outer_iter_num(const int32_t outer_iter_num) { _outer_iter_num = outer_iter_num; }
  void set_inner_iter_num(const int32_t inner_iter_num) { _inner_iter_num = inner_iter_num; }

 private:
  double _supply_unit = 0;
  double _initial_penalty = 0;
  double _penalty_drop_rate = 0;
  int32_t _outer_iter_num = 0;
  int32_t _inner_iter_num = 0;
};

}  // namespace irt
