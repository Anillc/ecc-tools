#pragma once

#include <cassert>
#include <memory>

#include "builder.h"
#include "Types.hpp"

namespace ircx {

class Net;
class Segment;
class Patch;
class Via;
class Pin;

class LayoutData;
class LayerTable;
class SpefContext;

class ParasiticXIDBAdapter {
 public:
  explicit ParasiticXIDBAdapter(::idb::IdbBuilder* idb) : idb_(idb) { assert(idb_); }
  ParasiticXIDBAdapter() = delete;
  ~ParasiticXIDBAdapter() = default;

  bool adapt(LayoutData&, LayerTable&, SpefContext&);

  void adaptLayerTable(::idb::IdbLayers* idb_layers);
  void adaptRoutingLayer(::idb::IdbLayers* idb_layers);

  void adaptSpefContext(::idb::IdbDesign* idb_design);

  void adaptNet(::idb::IdbNetList* idb_netlist);
  Pin adaptPin(::idb::IdbPin* idb_pin, bool is_driving);
  Segment adaptSegments(::idb::IdbRegularWireSegment* idb_seg);
  Patch adaptPatch(::idb::IdbRegularWireSegment* idb_seg);
  Via adaptVia(::idb::IdbVia* idb_via);

  void adaptSpecialNet(::idb::IdbSpecialNetList* idb_special_netlist);

 private:
  //idb
  ::idb::IdbBuilder* idb_{nullptr};
  // from outside
  LayoutData* layout_data_{nullptr};
  LayerTable* layer_table_{nullptr};
  SpefContext* spef_context_{nullptr};

  GtlRectI IdbRectToGtlRect(::idb::IdbRect*) const;
  GtlPointI IdbPointToGtlPoint(::idb::IdbCoordinate<int32_t>*) const;
};

} // namespace ircx