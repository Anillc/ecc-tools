#pragma once

#include "Types.hpp"
#include "EnvPool.hpp"

namespace ircx {

struct EtchInterval {
  Micron a0 = 0; // interval start
  Micron a1 = 0; // interval end
  // change with etch
  Micron center;
  Micron width;

  Micron lo_spacing;
  Micron hi_spacing;

  Micron thickness;
  Micron height;
};

class EtchPool { // of each net
 public:
  EtchPool() = default;
  ~EtchPool() = default;

  void append_edge_etch_interval_pool(std::vector<EtchInterval> v) {
    edge_interval_ranges_.emplace_back(etch_interval_pool_.size(), v.size());

    etch_interval_pool_.insert(etch_interval_pool_.end(),
                               std::make_move_iterator(v.begin()),
                               std::make_move_iterator(v.end()));
  }

  void clear() {
    etch_interval_pool_.clear();
    edge_interval_ranges_.clear();
  }

  std::span<const EtchInterval> edge_etch_interval_pool(Size edge_id) const {
      if (edge_id >= edge_interval_ranges_.size()) {
          return {};
      }
      const auto& [offset, length] = edge_interval_ranges_[edge_id];
      return std::span<const EtchInterval>(etch_interval_pool_.data() + offset, length);
  }
  std::span<EtchInterval> edge_etch_interval_pool(Size edge_id) {
      if (edge_id >= edge_interval_ranges_.size()) {
          return {};
      }
      const auto& [offset, length] = edge_interval_ranges_[edge_id];
      return std::span<EtchInterval>(etch_interval_pool_.data() + offset, length);
  }

 private:
  std::vector<EtchInterval> etch_interval_pool_;
  std::vector<std::pair<Size, Size>> edge_interval_ranges_;
};
}
