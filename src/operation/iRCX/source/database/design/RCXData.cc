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
#include "RCXData.hh"

#include <algorithm>
#include <utility>

#include "ProcessCorner.hpp"

namespace ircx {

RCXData::CornerData::CornerData() = default;
RCXData::CornerData::~CornerData() = default;
RCXData::CornerData::CornerData(CornerData&&) noexcept = default;
RCXData::CornerData& RCXData::CornerData::operator=(CornerData&&) noexcept = default;

RCXData::~RCXData() = default;

void RCXData::reset()
{
  layout_.clear();
  spef_context_.clear();
  layer_table_.clear();
  corners_.clear();
  mapping_builder_.clear();
  topo_pool_.clear();
  rc_table_.clear();
  process_layers_registered_ = false;
}

void RCXData::setDBData(LayoutData layout_data,
                        const LayerTable& design_layer_table,
                        SpefContext spef_context)
{
  layout_ = std::move(layout_data);
  layer_table_.copyDesignLayersFrom(design_layer_table);
  spef_context_ = std::move(spef_context);
  topo_pool_.clear();
  rc_table_.clear();
}

std::vector<::itf::ProcessCorner*> RCXData::corners()
{
  std::vector<::itf::ProcessCorner*> out;
  out.reserve(corners_.size());
  for (auto& corner : corners_) {
    out.push_back(corner.process_corner.get());
  }
  return out;
}

std::vector<const ::itf::ProcessCorner*> RCXData::corners() const
{
  std::vector<const ::itf::ProcessCorner*> result;
  result.reserve(corners_.size());
  for (const auto& corner : corners_) {
    result.push_back(corner.process_corner.get());
  }
  return result;
}

std::vector<const parser::CapTable*> RCXData::corner_cap_tables() const
{
  std::vector<const parser::CapTable*> result;
  result.reserve(corners_.size());
  for (const auto& corner : corners_) {
    result.push_back(&corner.cap_table);
  }
  return result;
}

bool RCXData::hasCorner(const Str& corner_name) const
{
  return std::any_of(corners_.begin(), corners_.end(),
                     [&](const CornerData& corner) {
                       return corner.name == corner_name;
                     });
}

}  // namespace ircx
