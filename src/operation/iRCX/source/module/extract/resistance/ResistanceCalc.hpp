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

#include <utility>
#include <vector>

#include "LayoutData.hpp"
#include "RCTable.hpp"
namespace ircx {

class LayerTable;
class TopoEdge;
class TopoPool;
}

namespace itf {
class ProcessCorner;
}

namespace ircx {

class ResistanceCalc {
 public:
  // Meyer's singleton
  static ResistanceCalc& getOrCreateInst() {
    static ResistanceCalc inst;
    return inst;
  }

  // Disallow copy/move
  ResistanceCalc(const ResistanceCalc&) = delete;
  ResistanceCalc& operator=(const ResistanceCalc&) = delete;
  ResistanceCalc(ResistanceCalc&&) = delete;
  ResistanceCalc& operator=(ResistanceCalc&&) = delete;

  void set_layout_data(const LayoutData* v) {
    layout_data_ = v;
    dbu_to_micron_ = Micron(1.0) / v->micron_to_dbu;
  }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_rc_table(RCTable* v) { rc_table_ = v; }
  void set_corners(const std::vector<itf::ProcessCorner*>& v) { corners_ = v; }

  void calc();
  std::pair<Micron, Micron> node_range(const TopoEdge& e) const;

 private:
  ResistanceCalc() = default;
  ~ResistanceCalc() = default;

  Micron dbu_to_micron_{1};

  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  std::vector<itf::ProcessCorner*> corners_;

  RCTable* rc_table_{nullptr};
};
}
