#pragma once

#include <span>
#include <vector>
#include <unordered_map>

#include "Types.hpp"
#include "HashFactory.hpp"
#include "TopoPool.hpp"

namespace ircx {

using CouplingKey     = HashFactory::UndirectedPairKey<Size>;
using CouplingKeyHash = HashFactory::PairKeyHash<CouplingKey>;

struct CcapEntry {
  Size edge_a;
  Size edge_b;
  Size corner_id;
  F32  value;
};

class RCTable {
 public:
  RCTable() = default;
  ~RCTable() = default;

  /// Pre-allocate all storage. Must be called before any parallel calc.
  void init(Size corner_num, Size net_num, const TopoPool& topo) {
    corner_num_ = corner_num;
    net_num_    = net_num;

    Size total = corner_num * net_num;
    corner_net_res_pools_.resize(total);
    corner_net_gcap_pools_.resize(total);

    for (Size corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
      for (Size net_idx = 0; net_idx < net_num; ++net_idx) {
        const Size edge_count = topo.net_edges(net_idx).size();
        const Size pool_idx = corner_idx * net_num + net_idx;
        corner_net_res_pools_[pool_idx].assign(edge_count, 0.0);
        corner_net_gcap_pools_[pool_idx].assign(edge_count, 0.0);
      }
    }

    net_ccap_entries_.resize(net_num);
    merged_ccap_.clear();
  }

  // Resistance: writable span per (corner, net)
  std::span<F64> corner_net_res_pool(Size corner_id, Size net_id) {
    return corner_net_res_pools_[corner_id * net_num_ + net_id];
  }
  std::span<const F64> corner_net_res_pool(Size corner_id, Size net_id) const {
    return corner_net_res_pools_[corner_id * net_num_ + net_id];
  }

  // Ground cap: writable span per (corner, net)
  std::span<F64> corner_net_gcap_pool(Size corner_id, Size net_id) {
    return corner_net_gcap_pools_[corner_id * net_num_ + net_id];
  }
  std::span<const F64> corner_net_gcap_pool(Size corner_id, Size net_id) const {
    return corner_net_gcap_pools_[corner_id * net_num_ + net_id];
  }

  // Coupling cap: per-net accumulation (parallel-safe across nets)
  void append_net_ccap_entry(Size net_id, Size edge_a, Size edge_b, Size corner_id, F32 value) {
    net_ccap_entries_[net_id].push_back({edge_a, edge_b, corner_id, value});
  }

  /// Merge per-net ccap entries into the final map.
  /// Call AFTER the parallel loop completes.
  void merge_net_ccap_entries() {
    for (auto& net_entries : net_ccap_entries_) {
      for (auto& [edge_a_id, edge_b_id, corner_idx, cap_value] : net_entries) {
        CouplingKey key(edge_a_id, edge_b_id);
        auto it = merged_ccap_.find(key);
        if (it == merged_ccap_.end()) {
          auto& vec = merged_ccap_[key];
          vec.resize(corner_num_, 0.0f);
          vec[corner_idx] += cap_value;
        } else {
          it->second[corner_idx] += cap_value;
        }
      }
      net_entries.clear();
    }
  }

  // Accessors
  Size corner_num() const { return corner_num_; }
  Size net_num()    const { return net_num_; }
  const auto& merged_ccap() const { return merged_ccap_; }

 private:
  Size corner_num_{0};
  Size net_num_{0};

  // indexed by [corner_id * net_num_ + net_id], each vec size = edge count of net
  std::vector<std::vector<F64>> corner_net_res_pools_;
  std::vector<std::vector<F64>> corner_net_gcap_pools_;

  // parallel accumulation: indexed by net_id
  std::vector<std::vector<CcapEntry>> net_ccap_entries_;

  // merged result
  std::unordered_map<CouplingKey, std::vector<F32>, CouplingKeyHash> merged_ccap_;
};

}
