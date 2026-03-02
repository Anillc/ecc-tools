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

#include "RAGCell.hpp"
#include "RANet.hpp"
#include "RAComParam.hpp"

namespace irt {

class RAModel
{
 public:
  RAModel() = default;
  ~RAModel() = default;
  // getter
  std::vector<RANet>& get_ra_net_list() { return _ra_net_list; }
    RAComParam& get_ra_com_param() { return _ra_com_param; }
  std::vector<RAGCell>& get_ra_gcell_list() { return _ra_gcell_list; }
  std::vector<double>& get_result_list() { return _result_list; }
  std::vector<double>& get_nabla_f_col() { return _nabla_f_col; }
  std::vector<double>& get_nabla_f_row() { return _nabla_f_row; }
  double get_alpha() const { return _alpha; }
  // setter
  void set_ra_net_list(const std::vector<RANet>& ra_net_list) { _ra_net_list = ra_net_list; }
  void set_ra_com_param(const RAComParam& ra_com_param) { _ra_com_param = ra_com_param; }
  void set_alpha(const double alpha) { _alpha = alpha; }
  // function

 private:
  std::vector<RANet> _ra_net_list;
  RAComParam _ra_com_param;
  std::vector<RAGCell> _ra_gcell_list;
  std::vector<double> _result_list;
  std::vector<double> _nabla_f_col;
  std::vector<double> _nabla_f_row;
  double _alpha = 0;
};

}  // namespace irt
