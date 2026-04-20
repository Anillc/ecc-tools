#pragma once

#include "TopoPool.hpp"
#include "Types.hpp"

namespace ircx
{

class MetalDensity
{
 public:
  MetalDensity() = default;
  ~MetalDensity() = default;

  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }

  // Build the per-layer polygon set from all edges (regular + special).
  // Special-net edges are stored in a dedicated special_edge_pool() in TopoPool.
  void build()
  {
    polyset_map_.clear();

    // regular net edges
    for (const auto& edge : topo_pool_->edge_pool()) {
      polyset_map_[edge.layer_id()] += edge.shape();
    }

    // special net edges (power/ground)
    for (const auto& edge : topo_pool_->special_edge_pool()) {
      polyset_map_[edge.layer_id()] += edge.shape();
    }
  }

  double cal_density(Size layer, const GtlRectI& box) const
  {
    const auto it = polyset_map_.find(layer);
    if (it == polyset_map_.end()) return 0.0;

    return static_cast<double>(gtl::area(it->second & box)) /
           static_cast<double>(gtl::area(box));
  }

 private:
  // copy
  MetalDensity(const MetalDensity&) = delete;
  MetalDensity& operator=(const MetalDensity&) = delete;
  // move
  MetalDensity(MetalDensity&&) = delete;
  MetalDensity& operator=(MetalDensity&&) = delete;

 private:
  const TopoPool* topo_pool_{nullptr};
  std::map<Size, GtlPolysetI> polyset_map_;
};

} // namespace ircx
