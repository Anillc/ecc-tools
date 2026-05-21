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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CapTable.hpp"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "MappingBuilder.hpp"
#include "RCTable.hh"
#include "SpefContext.hh"
#include "TopoPool.hh"
#include "Types.hh"

namespace itf {
class ProcessCorner;
}  // namespace itf

namespace ircx {

namespace parser {
class CapTable;
class MappingBuilder;
}  // namespace parser

#define RCX_DATA_INST (ircx::RCXData::getInst())

class RCXData final {
 public:
  struct CornerData {
    CornerData();
    ~CornerData();
    CornerData(CornerData&&) noexcept;
    CornerData& operator=(CornerData&&) noexcept;
    CornerData(const CornerData&) = delete;
    CornerData& operator=(const CornerData&) = delete;

    Str name;
    Str itf_file;
    Str captab_file;
    std::unique_ptr<::itf::ProcessCorner> process_corner;
    parser::CapTable cap_table;
  };

  static RCXData& getInst() {
    static RCXData inst;
    return inst;
  }

  RCXData(const RCXData&) = delete;
  RCXData& operator=(const RCXData&) = delete;
  RCXData(RCXData&&) = delete;
  RCXData& operator=(RCXData&&) = delete;

  void reset();
  void setDBData(LayoutData layout_data,
                 const LayerTable& design_layer_table,
                 SpefContext spef_context);

  LayoutData& layout() { return layout_; }
  const LayoutData& layout() const { return layout_; }
  SpefContext& spef_context() { return spef_context_; }
  const SpefContext& spef_context() const { return spef_context_; }
  LayerTable& layer_table() { return layer_table_; }
  const LayerTable& layer_table() const { return layer_table_; }
  parser::MappingBuilder& mapping_builder() { return mapping_builder_; }
  const parser::MappingBuilder& mapping_builder() const { return mapping_builder_; }
  TopoPool& topo_pool() { return topo_pool_; }
  const TopoPool& topo_pool() const { return topo_pool_; }
  RCTable& rc_table() { return rc_table_; }
  const RCTable& rc_table() const { return rc_table_; }
  std::vector<CornerData>& corner_data() { return corners_; }
  const std::vector<CornerData>& corner_data() const { return corners_; }

  std::vector<::itf::ProcessCorner*> corners();
  std::vector<const ::itf::ProcessCorner*> corners() const;
  std::vector<const parser::CapTable*> corner_cap_tables() const;

  [[nodiscard]] bool hasCorner(const Str& corner_name) const;
  void setProcessLayersRegistered(bool value) { process_layers_registered_ = value; }
  [[nodiscard]] bool processLayersRegistered() const { return process_layers_registered_; }

 private:
  RCXData() = default;
  ~RCXData();

  LayoutData layout_;
  SpefContext spef_context_;
  LayerTable layer_table_;
  std::vector<CornerData> corners_;
  parser::MappingBuilder mapping_builder_;
  TopoPool topo_pool_;
  RCTable rc_table_;
  bool process_layers_registered_{false};
};

}  // namespace ircx
