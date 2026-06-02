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
#include "DumpNetShapeTool.hh"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include "Geometry.hh"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "RCXData.hh"
#include "log/Log.hh"

namespace ircx {
namespace {

enum class ShapeCode : std::size_t
{
  kSegment = 0,
  kPatch,
  kViaNonCut,
  kViaCut,
  kPinNonCut,
  kPinCut,
};

constexpr std::array<char, 6> kShapeCodeNames = {'A', 'B', 'C', 'D', 'E', 'F'};

struct LayerShapes
{
  std::array<std::vector<GtlRectI>, kShapeCodeNames.size()> rects;
};

using NetShapesByLayer = std::map<Size, LayerShapes>;

auto shapeIndex(ShapeCode code) -> std::size_t
{
  return static_cast<std::size_t>(code);
}

auto validLayer(Size layer_id) -> bool
{
  return layer_id != kMaxSize;
}

auto layerName(const LayoutData& layout, const LayerTable& layer_table, Size layer_id) -> Str
{
  if (auto it = layout.routing_layers.find(layer_id); it != layout.routing_layers.end()) {
    return it->second.layer_name();
  }

  try {
    return layer_table.design_name(layer_id);
  } catch (const std::out_of_range&) {
    return "UNKNOWN";
  }
}

void addRect(NetShapesByLayer& shapes, Size layer_id, ShapeCode code, const GtlRectI& rect)
{
  if (!validLayer(layer_id)) {
    return;
  }
  shapes[layer_id].rects[shapeIndex(code)].push_back(rect);
}

auto isCutLayer(Size layer_id, const std::set<Size>& cut_layer_ids) -> bool
{
  return cut_layer_ids.find(layer_id) != cut_layer_ids.end();
}

void collectCutLayers(const Net& net, std::set<Size>& cut_layer_ids)
{
  for (const Via& via : net.vias) {
    if (validLayer(via.layer_rect_cut.first)) {
      cut_layer_ids.insert(via.layer_rect_cut.first);
    }
  }
}

auto buildLayerOrder(const LayoutData& layout) -> std::vector<Size>
{
  std::set<Size> cut_layer_ids;
  std::map<std::pair<Size, Size>, Size> cut_by_routing_pair;
  std::set<Size> layer_ids_with_shapes;

  auto remember_via = [&](const Via& via) {
    if (validLayer(via.layer_rect_btm.first)) {
      layer_ids_with_shapes.insert(via.layer_rect_btm.first);
    }
    if (validLayer(via.layer_rect_top.first)) {
      layer_ids_with_shapes.insert(via.layer_rect_top.first);
    }
    if (validLayer(via.layer_rect_cut.first)) {
      layer_ids_with_shapes.insert(via.layer_rect_cut.first);
      cut_layer_ids.insert(via.layer_rect_cut.first);
    }

    const Size lower = std::min(via.layer_rect_btm.first, via.layer_rect_top.first);
    const Size upper = std::max(via.layer_rect_btm.first, via.layer_rect_top.first);
    if (validLayer(lower) && validLayer(upper) && validLayer(via.layer_rect_cut.first)) {
      cut_by_routing_pair[{lower, upper}] = via.layer_rect_cut.first;
    }
  };

  auto remember_net = [&](const Net& net) {
    for (const Segment& segment : net.segments) {
      if (validLayer(segment.layer_id)) {
        layer_ids_with_shapes.insert(segment.layer_id);
      }
    }
    for (const Patch& patch : net.patches) {
      if (validLayer(patch.layer_id)) {
        layer_ids_with_shapes.insert(patch.layer_id);
      }
    }
    for (const Via& via : net.vias) {
      remember_via(via);
    }
    for (const Pin& pin : net.pins) {
      for (const auto& [layer_id, rect] : pin.layer_id_rects) {
        (void) rect;
        if (validLayer(layer_id)) {
          layer_ids_with_shapes.insert(layer_id);
        }
      }
    }
  };

  for (const Net& net : layout.net_vec) {
    remember_net(net);
  }
  remember_net(layout.special_net);

  std::vector<Size> routing_layer_ids;
  routing_layer_ids.reserve(layout.routing_layers.size());
  for (const auto& [layer_id, routing_layer] : layout.routing_layers) {
    (void) routing_layer;
    routing_layer_ids.push_back(layer_id);
    layer_ids_with_shapes.insert(layer_id);
  }
  std::sort(routing_layer_ids.begin(), routing_layer_ids.end());

  const Size first_cut_layer_id = routing_layer_ids.empty() ? kMaxSize : routing_layer_ids.back() + 1;
  for (std::size_t i = 0; i + 1 < routing_layer_ids.size(); ++i) {
    const Size cut_layer_id = first_cut_layer_id + i;
    cut_layer_ids.insert(cut_layer_id);
    layer_ids_with_shapes.insert(cut_layer_id);
    cut_by_routing_pair.try_emplace({routing_layer_ids[i], routing_layer_ids[i + 1]}, cut_layer_id);
  }

  std::vector<Size> ordered;
  ordered.reserve(layer_ids_with_shapes.size());
  std::set<Size> emitted;
  auto emit = [&](Size layer_id) {
    if (validLayer(layer_id) && emitted.insert(layer_id).second) {
      ordered.push_back(layer_id);
    }
  };

  for (std::size_t i = 0; i < routing_layer_ids.size(); ++i) {
    emit(routing_layer_ids[i]);
    if (i + 1 >= routing_layer_ids.size()) {
      continue;
    }
    const auto it = cut_by_routing_pair.find({routing_layer_ids[i], routing_layer_ids[i + 1]});
    if (it != cut_by_routing_pair.end()) {
      emit(it->second);
    }
  }

  for (Size layer_id : layer_ids_with_shapes) {
    if (cut_layer_ids.find(layer_id) == cut_layer_ids.end()) {
      emit(layer_id);
    }
  }
  for (Size layer_id : cut_layer_ids) {
    emit(layer_id);
  }

  return ordered;
}

auto collectNetShapes(const Net& net, const std::set<Size>& cut_layer_ids) -> NetShapesByLayer
{
  NetShapesByLayer shapes;

  for (const Segment& segment : net.segments) {
    addRect(shapes, segment.layer_id, ShapeCode::kSegment, segment.rect);
  }
  for (const Patch& patch : net.patches) {
    addRect(shapes, patch.layer_id, ShapeCode::kPatch, patch.rect);
  }
  for (const Via& via : net.vias) {
    addRect(shapes, via.layer_rect_btm.first, ShapeCode::kViaNonCut, via.layer_rect_btm.second);
    addRect(shapes, via.layer_rect_top.first, ShapeCode::kViaNonCut, via.layer_rect_top.second);
    addRect(shapes, via.layer_rect_cut.first, ShapeCode::kViaCut, via.layer_rect_cut.second);
  }
  for (const Pin& pin : net.pins) {
    for (const auto& [layer_id, rect] : pin.layer_id_rects) {
      addRect(shapes, layer_id, isCutLayer(layer_id, cut_layer_ids) ? ShapeCode::kPinCut : ShapeCode::kPinNonCut, rect);
    }
  }

  return shapes;
}

auto hasShapes(const LayerShapes& shapes) -> bool
{
  return std::any_of(shapes.rects.begin(), shapes.rects.end(), [](const auto& rects) { return !rects.empty(); });
}

auto outputFileName(const LayoutData& layout) -> Str
{
  return (layout.design_name.empty() ? "design" : layout.design_name) + ".shape";
}

void writeRect(std::ostream& os, const GtlRectI& rect)
{
  os << '[' << geom::min_x(rect) << ' ' << geom::min_y(rect) << ' ' << geom::max_x(rect) << ' ' << geom::max_y(rect) << ']';
}

void writeHeader(std::ostream& os, const LayoutData& layout, const LayerTable& layer_table, const std::vector<Size>& layer_order)
{
  os << "# dump_net_shape\n";
  os << "# layer_order low_to_high: index layer_id design_layer_name\n";
  for (std::size_t i = 0; i < layer_order.size(); ++i) {
    os << "# L " << i << ' ' << layer_order[i] << ' ' << layerName(layout, layer_table, layer_order[i]) << '\n';
  }
  os << "# shape_code: A=Segment B=Patch C=Via_non_cut_layer D=Via_cut_layer E=Pin_non_cut_layer F=Pin_cut_layer\n";
  os << "# rect format: [llx lly urx ury] in DBU\n";
}

void writeNet(std::ostream& os, const Net& net, const LayoutData& layout, const LayerTable& layer_table,
              const std::vector<Size>& layer_order, const std::set<Size>& cut_layer_ids)
{
  const NetShapesByLayer net_shapes = collectNetShapes(net, cut_layer_ids);

  os << "NET " << net.name << '\n';
  for (Size layer_id : layer_order) {
    const auto it = net_shapes.find(layer_id);
    if (it == net_shapes.end() || !hasShapes(it->second)) {
      continue;
    }

    os << "L " << layer_id << ' ' << layerName(layout, layer_table, layer_id) << '\n';
    for (std::size_t i = 0; i < kShapeCodeNames.size(); ++i) {
      const auto& rects = it->second.rects[i];
      if (rects.empty()) {
        continue;
      }

      os << kShapeCodeNames[i];
      for (const GtlRectI& rect : rects) {
        os << ' ';
        writeRect(os, rect);
      }
      os << '\n';
    }
  }
  os << "END_NET\n";
}

}  // namespace

auto DumpNetShapeTool::run() -> bool
{
  const RCXData& data = RCX_DATA_INST;
  const LayoutData& layout = data.layout();
  if (layout.empty()) {
    LOG_ERROR << "dump_net_shape failed: layout data is empty. Run init_rcx/adapt DB before dumping shapes.";
    return false;
  }

  const Str output_file = outputFileName(layout);
  std::ofstream ofs(output_file);
  if (!ofs) {
    LOG_ERROR << "dump_net_shape failed: cannot open output file " << output_file;
    return false;
  }

  std::set<Size> cut_layer_ids;
  for (const Net& net : layout.net_vec) {
    collectCutLayers(net, cut_layer_ids);
  }
  collectCutLayers(layout.special_net, cut_layer_ids);

  const std::vector<Size> layer_order = buildLayerOrder(layout);
  writeHeader(ofs, layout, data.layer_table(), layer_order);

  for (const Net& net : layout.net_vec) {
    writeNet(ofs, net, layout, data.layer_table(), layer_order, cut_layer_ids);
  }

  if (!layout.special_net.segments.empty() || !layout.special_net.patches.empty() || !layout.special_net.vias.empty()
      || !layout.special_net.pins.empty()) {
    Net special_net = layout.special_net;
    special_net.name = "__SPECIAL_NET__";
    writeNet(ofs, special_net, layout, data.layer_table(), layer_order, cut_layer_ids);
  }
  if (!ofs) {
    LOG_ERROR << "dump_net_shape failed: cannot write output file " << output_file;
    return false;
  }

  LOG_INFO << "dump_net_shape wrote " << output_file;
  return true;
}

}  // namespace ircx
