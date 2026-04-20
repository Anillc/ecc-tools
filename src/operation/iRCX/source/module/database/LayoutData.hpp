#pragma once

#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Types.hpp"
#include "RoutingLayer.hpp"
#include "log/Log.hh"

namespace ircx {

// ============================================================
// Segment
// ============================================================
struct Segment {
  Size layer_id{kMaxSize};
  GtlRectI  rect;

  GtlPointI p0, p1;
};

// ============================================================
// Patch
// ============================================================
struct Patch {
  Size layer_id{kMaxSize};
  GtlRectI  rect;
};

// ============================================================
// Pin
// ============================================================
struct Pin {
  Str name; // pin(inst:pin)/port name

  bool is_driver{false};
  bool is_input{false};
  bool is_output{false};

  std::vector<std::pair<Size, GtlRectI>> layer_id_rects;

  bool is_port() const {
    return name.find(':') == Str::npos;
  }

  Str instance_name() const {
    size_t pos = name.find(':');
    return name.substr(0, pos);
  }
  Str instance_pin_name() const {
    size_t pos = name.find(':');
    return name.substr(pos+1);
  }
  Str port_name() const {
    return name;
  }
};

// ============================================================
// Via
// ============================================================
struct Via {
  Str name;

  GtlPointI point;
  // Top/Cut/Bottom layer rect
  std::pair<Size, GtlRectI> layer_rect_top;
  std::pair<Size, GtlRectI> layer_rect_cut;
  std::pair<Size, GtlRectI> layer_rect_btm;
};

// ============================================================
// Net
// ============================================================
struct Net {
  Size id;
  Str name;
  std::vector<Segment> segments;
  std::vector<Patch> patches;
  std::vector<Via> vias;
  std::vector<Pin> pins;
};

struct LayoutData {
  // Design metadata
  Str design_name;
  GtlRectI die_shape;
  Dbu micron_to_dbu{1};

  // Technology layers
  std::map<Size, RoutingLayer> routing_layers;

  // Net metadata
  // starting from 0.
  std::vector<Net> net_vec; // net id is index of net_vec

  // Special-net geometry (power/ground, no connectivity graph needed)
  Net special_net;

  // Helpers
  Size regular_net_count() const { return net_vec.size(); }
};

}  // namespace ircx
