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

#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include "Types.hh"

namespace ircx {

template <typename Interval>
class IntervalPool
{
 public:
  IntervalPool() = default;
  ~IntervalPool() = default;

  void append_edge_intervals(std::vector<Interval> intervals)
  {
    edge_interval_ranges_.emplace_back(interval_pool_.size(), intervals.size());
    interval_pool_.insert(interval_pool_.end(),
                          std::make_move_iterator(intervals.begin()),
                          std::make_move_iterator(intervals.end()));
  }

  [[nodiscard]] std::span<const Interval> edge_intervals(Size edge_id) const
  {
    if (edge_id >= edge_interval_ranges_.size()) {
      return {};
    }

    const auto& [offset, length] = edge_interval_ranges_[edge_id];
    if (length == 0) {
      return {};
    }
    return std::span<const Interval>(interval_pool_.data() + offset, length);
  }

  [[nodiscard]] std::span<Interval> edge_intervals(Size edge_id)
  {
    if (edge_id >= edge_interval_ranges_.size()) {
      return {};
    }

    const auto& [offset, length] = edge_interval_ranges_[edge_id];
    if (length == 0) {
      return {};
    }
    return std::span<Interval>(interval_pool_.data() + offset, length);
  }

  void clear()
  {
    interval_pool_.clear();
    edge_interval_ranges_.clear();
  }

 private:
  std::vector<Interval> interval_pool_;
  std::vector<std::pair<Size, Size>> edge_interval_ranges_;
};

}  // namespace ircx
