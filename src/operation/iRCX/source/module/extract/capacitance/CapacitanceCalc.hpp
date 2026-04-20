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

#include <vector>

#include "LayoutData.hpp"
#include "EnvPool.hpp"
#include "RCTable.hpp"
namespace ircx {

namespace parser {
class CapTable;
}

class LayoutData;
class LayerTable;
class TopoPool;
}

namespace itf {
class ProcessCorner;
}

namespace ircx {

class CapacitanceCalc {
 public:
  // Meyer's singleton
  static CapacitanceCalc& getOrCreateInst() {
    static CapacitanceCalc inst;
    return inst;
  }

  // Disallow copy/move
  CapacitanceCalc(const CapacitanceCalc&) = delete;
  CapacitanceCalc& operator=(const CapacitanceCalc&) = delete;
  CapacitanceCalc(CapacitanceCalc&&) = delete;
  CapacitanceCalc& operator=(CapacitanceCalc&&) = delete;

  void set_layout_data(const LayoutData* v) {
    layout_data_ = v;
    net_num_ = v->regular_net_count();
    dbu_to_micron_ = Micron(1.0) / v->micron_to_dbu;
  }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_cap_tables(const std::vector<const parser::CapTable*>& v) {
    cap_tables_ = v;
  }
  void set_corners(const std::vector<itf::ProcessCorner*>& v) {
    corners_ = v;
    corner_num_ = v.size();
  }
  void set_rc_table(RCTable* v) { rc_table_ = v; }

  void calc();

 private:
  CapacitanceCalc() = default;
  ~CapacitanceCalc() = default;

  // Determine below/above process layer names from cross-layer segments.
  void resolveCrossLayers(
      const CrossOverlapSub* crossSeg,
      Str& belowLayer,
      Str& aboveLayer) const;

  Size net_num_{0};
  Size corner_num_{0};
  Micron dbu_to_micron_{1};

  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  std::vector<const parser::CapTable*> cap_tables_;
  std::vector<itf::ProcessCorner*> corners_;

  RCTable* rc_table_{nullptr};
};

}  // namespace ircx
