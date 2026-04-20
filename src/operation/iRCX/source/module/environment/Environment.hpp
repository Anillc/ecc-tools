#pragma once

#include <map>
#include <vector>

#include "Geoms.hpp"
#include "Types.hpp"

#include "Track.hpp"
#include "Pixel.hpp"

#include "EnvPool.hpp"

namespace ircx {

class TopoPool;
class LayoutData;

class Environment final {
 public:
  // Meyer's singleton
  static Environment& getOrCreateInst() {
    static Environment inst;
    return inst;
  }

  // Disallow copy/move
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;
  Environment(Environment&&) = delete;
  Environment& operator=(Environment&&) = delete;

  void set_layout_data(const LayoutData* v) { layout_data_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }

  void buildNetEnvPools();

  // getter
  const std::vector<EnvPool>& net_env_pools() const { return net_env_pools_; }
  const EnvPool& net_env_pool(Size net_id) const {
    LOG_FATAL_IF(net_id >= net_env_pools_.size()) << "net_id out of range.";
    return net_env_pools_[net_id];
  }
 private:
  Environment() = default;
  ~Environment() = default;

  void buildTracks();
  void buildPixels();
  void buildSearchTrackNumMap();

  const LayoutData* layout_data_{nullptr};
  const TopoPool* topo_pool_{nullptr};

  F32 bucket_size_um_{kDefaultBucketUm};
  F32 window_size_um_{kDefaultWindowUm};

  Size cross_layer_{3};

  std::map<Size, Pixel> layer_to_pixel_;
  std::map<Size, Track> layer_to_track_;  // preferred routing direction only
  std::map<Size, Dbu> layer_to_search_track_num_;

  std::vector<EnvPool> net_env_pools_;
};

} // namespace ircx
