#pragma once

#include <vector>

#include "RCTable.hpp"
#include "LayoutData.hpp"
#include "TopoPool.hpp"
#include "ProcessCorner.hpp"

namespace ircx {

class LayoutData;
class TopoPool;
class LayerTable;

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
  void set_corners(const std::vector<::itf::ProcessCorner*>& v) { corners_ = v; }

  void calc();
  std::pair<Micron, Micron> node_range(const TopoEdge& e) const;

 private:
  ResistanceCalc() = default;
  ~ResistanceCalc() = default;

  Micron dbu_to_micron_{1};

  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  std::vector<::itf::ProcessCorner*> corners_;

  RCTable* rc_table_{nullptr};
};
}
