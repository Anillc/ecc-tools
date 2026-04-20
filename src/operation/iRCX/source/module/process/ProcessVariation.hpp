#pragma once

#include <memory>
#include <span>
#include <vector>

#include "EtchPool.hpp"
#include "Environment.hpp"
#include "MetalDensity.hpp"
#include "Geoms.hpp"
#include "Types.hpp"

namespace ircx {
  class Environment;
  class ProcessCorner;
  class MetalDensity;
  class LayerTable;
  class TopoPool;
}

namespace itf {
  class ProcessCorner;
}

namespace ircx {

class ProcessVariation final { // singleton
 public:
  // Meyer's singleton
  static ProcessVariation& getOrCreateInst() {
    static ProcessVariation inst;  // C++11 thread-safe
    return inst;
  }

  // Disallow copy/move
  ProcessVariation(const ProcessVariation&) = delete;
  ProcessVariation& operator=(const ProcessVariation&) = delete;
  ProcessVariation(ProcessVariation&&) = delete;
  ProcessVariation& operator=(ProcessVariation&&) = delete;

  void set_layout_data(const LayoutData* v) { layout_data_ = v; }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_corners(const std::vector<::itf::ProcessCorner*>& v) { corners_ = v; }

  std::vector<::itf::ProcessCorner*>& corners() { return corners_; }
  const std::vector<::itf::ProcessCorner*>& corners() const { return corners_; }
  Size corner_num() const { return corner_num_; }

  EtchPool& corner_net_etch_pool(Size corner_idx, Size net_id);
  const EtchPool& corner_net_etch_pool(Size corner_idx, Size net_id) const;

  // other built data
  const MetalDensity* metal_density() const { return &metal_density_; }

  // entry points
  void buildEtchPools();

 private:
  ProcessVariation() = default;
  ~ProcessVariation() = default;

  void initMetalDensity();
  void initEtchIntervals();

  Size corner_net_pool_index(Size corner_idx, Size net_id) const {
    return corner_idx * net_num_ + net_id;
  }

  // set from outside
  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  std::vector<::itf::ProcessCorner*> corners_{};

  // built here
  MetalDensity metal_density_;

  Size corner_num_{0};

  Size net_num_{0};
  std::vector<EtchPool> corner_net_etch_pools_;
};

} // namespace ircx
