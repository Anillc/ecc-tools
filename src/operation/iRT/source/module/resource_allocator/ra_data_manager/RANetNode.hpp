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

class RANetNode
{
 public:
  RANetNode() = default;
  RANetNode(const int32_t ra_net_idx, const int32_t result_idx)
  {
    _ra_net_idx = ra_net_idx;
    _result_idx = result_idx;
  }
  ~RANetNode() = default;
  // getter
  int32_t get_ra_net_idx() const { return _ra_net_idx; }
  int32_t get_result_idx() const { return _result_idx; }
  // setter
  // function
 private:
  int32_t _ra_net_idx = -1;
  int32_t _result_idx = -1;
};

}  // namespace irt
