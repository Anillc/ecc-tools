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

#include <fstream>
#include <map>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "Types.hh"
namespace itf {
  class ProcessCorner;
}

namespace ircx {

class LayoutData;
class TopoPool;
class LayerTable;
class RCTable;
class TopoNode;
class TopoEdge;
class SpefContext;

class SpefDumper {
 public:
  SpefDumper() = default;
  ~SpefDumper() = default;

  void set_spef_context(const SpefContext* v) { spef_context_ = v; }
  void set_layout_data(const LayoutData* v) { layout_data_ = v; }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_rc_table(const RCTable* v) { rc_table_ = v; }
  void set_corners(const std::vector<::itf::ProcessCorner*>& v) { corners_ = v; }

  // Dump one .spef file for the given corner.
  // corner_name: technology string (e.g. "typ", "slow", "fast") used in the
  //              output filename.
  // corner_idx:  index into RCTables vectors.
  void dump(const Str& output_dir, Size corner_idx) const;

 private:
  // Name-map helpers
  struct NameMaps {
    std::unordered_map<Str, int> net_name_to_id;
    std::unordered_map<Str, int> inst_name_to_id;
    std::unordered_map<Str, int> port_name_to_id;
    std::map<int, Str> net_id_to_name;
    std::map<int, Str> inst_id_to_name;
    std::map<int, Str> port_id_to_name;
    int next_id{1};
  };

  struct CouplingRef {
    Size self_edge_id{kMaxSize};
    Size other_edge_id{kMaxSize};
    double cap_ff{0.0};
  };

  void buildNameMaps() const;
  void buildPortIo() const;
  void buildNodeSpefNames() const;
  void buildNetSpefNames() const;
  void buildCouplingRefs(Size corner_idx) const;

  // Return the SPEF node-name string for a given node.
  //   - Port pin node      : "*<port_spef_id>"
  //   - Instance pin node  : "*<inst_spef_id>:<pin_name>"
  //   - Internal node      : "*<net_spef_id>:<local_idx + 1>"
  Str nodeName(const TopoNode& node) const;

  // Write helpers
  void writeHeader(std::ofstream& ofs) const;
  void writeNameMap(std::ofstream& ofs) const;
  void writePorts(std::ofstream& ofs) const;

  void writeDNet(std::ostream& os, Size corner_idx, Size net_idx) const;

  const SpefContext* spef_context_{nullptr};
  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  const RCTable* rc_table_{nullptr};
  std::vector<::itf::ProcessCorner*> corners_{};

  mutable NameMaps name_maps_;
  mutable std::unordered_map<Str, char> port_io_;
  mutable std::vector<Str> node_spef_names_;
  mutable std::vector<Str> net_spef_names_;
  mutable std::vector<std::vector<CouplingRef>> net_coupling_refs_;
  mutable std::vector<Str> net_str_buffer_;
};

}  // namespace ircx
