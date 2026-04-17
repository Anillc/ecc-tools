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
 * @file DesignCommitter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "Net.hh"
#include "Pin.hh"

namespace icts {
class CtsDBWrapper;
class CtsDesign;

class DesignCommitter
{
 public:
  struct RuntimeContext
  {
    CtsDesign* _design = nullptr;
    CtsDBWrapper* _db_wrapper = nullptr;
    std::function<bool(const Point&)> _is_in_die;
    std::function<void(Net*)> _register_synthesis_net;
    std::function<void()> _sync_timing;

    [[nodiscard]] bool isValid() const
    {
      return _design != nullptr && _db_wrapper != nullptr && _is_in_die != nullptr && _register_synthesis_net != nullptr
             && _sync_timing != nullptr;
    }
  };

  static DesignCommitter& getInst();

  DesignCommitter(const DesignCommitter&) = delete;
  DesignCommitter(DesignCommitter&&) = delete;
  DesignCommitter& operator=(const DesignCommitter&) = delete;
  DesignCommitter& operator=(DesignCommitter&&) = delete;

  void commit(const std::vector<Net*>& nets, const RuntimeContext& runtime_context) const;
  static std::pair<Point, Point> buildSnakeGuidePoints(const Point& parent_loc, const Point& current_loc, int64_t require_snake,
                                                       const std::function<bool(const Point&)>& is_in_die);

 private:
  DesignCommitter() = default;
  ~DesignCommitter() = default;

  void synthesisPin(Pin* pin, CtsDesign& design, CtsDBWrapper& db_wrapper) const;
  void synthesisNet(Net* net, const RuntimeContext& runtime_context) const;
};
}  // namespace icts
