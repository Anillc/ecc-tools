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
#include "ResistanceCalc.hh"

#include <algorithm>
#include <optional>
#include <stdexcept>

#include "LayerTable.hh"
#include "LayoutData.hh"
#include "TopoPool.hh"
#include "ProcessCorner.hpp"
#include "ResistanceTemperature.hh"
#include "log/Log.hh"
namespace ircx {

namespace {

struct TemperatureCoefficients
{
  F64 crt1 = 0.0;
  F64 crt2 = 0.0;
};

TemperatureCoefficients get_conductor_temperature_coefficients(
    const itf::LayerConductor& layer,
    Micron width)
{
  TemperatureCoefficients coefficients;

  std::optional<double> width_crt1;
  std::optional<double> width_crt2;
  layer.query_crt_vs_si_width(width, width_crt1, width_crt2);

  if (width_crt1.has_value()) {
    coefficients.crt1 = width_crt1.value();
  } else if (auto crt1 = layer.get_crt1()) {
    coefficients.crt1 = crt1.value();
  }

  if (width_crt2.has_value()) {
    coefficients.crt2 = width_crt2.value();
  } else if (auto crt2 = layer.get_crt2()) {
    coefficients.crt2 = crt2.value();
  }

  return coefficients;
}

TemperatureCoefficients get_via_temperature_coefficients(
    const itf::LayerVia& layer,
    F64 area)
{
  TemperatureCoefficients coefficients;

  std::optional<double> area_crt1;
  std::optional<double> area_crt2;
  layer.query_crt_vs_area(area, area_crt1, area_crt2);

  if (area_crt1.has_value()) {
    coefficients.crt1 = area_crt1.value();
  } else if (auto crt1 = layer.get_crt1()) {
    coefficients.crt1 = crt1.value();
  }

  if (area_crt2.has_value()) {
    coefficients.crt2 = area_crt2.value();
  } else if (auto crt2 = layer.get_crt2()) {
    coefficients.crt2 = crt2.value();
  }

  return coefficients;
}

}  // namespace

bool ResistanceCalc::ProcessLayerResolver::build(
    const LayerTable& layer_table,
    const itf::ProcessCorner& corner,
    const TopoPool& topo_pool,
    const Str& corner_name)
{
  conductors_.clear();
  vias_.clear();

  if (corner.get_layers() == nullptr) {
    LOG_ERROR << "calculate resistance failed: process layers missing for corner "
              << corner_name << ".";
    return false;
  }

  for (const TopoEdge& edge : topo_pool.edge_pool()) {
    const Size design_layer_id = edge.layer_id();

    Str design_layer_name;
    Str process_layer_name;
    Size process_layer_id = kMaxSize;
    try {
      design_layer_name = layer_table.design_name(design_layer_id);
      process_layer_id = layer_table.design_to_process_id(design_layer_id);
      process_layer_name = layer_table.process_name(process_layer_id);
    } catch (const std::out_of_range&) {
      LOG_ERROR << "calculate resistance failed: layer mapping missing, corner="
                << corner_name << ", design_layer_id=" << design_layer_id << ".";
      return false;
    }

    if (edge.is_via()) {
      if (vias_.contains(design_layer_id)) {
        continue;
      }

      const itf::LayerVia* via_layer =
          corner.get_layers()->find_via_layer(process_layer_id);
      if (via_layer == nullptr) {
        LOG_ERROR << "calculate resistance failed: via layer not found, corner="
                  << corner_name << ", design_layer_id=" << design_layer_id
                  << ", design_layer=" << design_layer_name
                  << ", process_layer_id=" << process_layer_id
                  << ", process_layer=" << process_layer_name << ".";
        return false;
      }
      vias_.emplace(design_layer_id, via_layer);
      continue;
    }

    if (conductors_.contains(design_layer_id)) {
      continue;
    }

    const itf::LayerConductor* conductor_layer =
        corner.get_layers()->find_conductor_layer(process_layer_id);
    if (conductor_layer == nullptr) {
      LOG_ERROR << "calculate resistance failed: conductor layer not found, corner="
                << corner_name << ", design_layer_id=" << design_layer_id
                << ", design_layer=" << design_layer_name
                << ", process_layer_id=" << process_layer_id
                << ", process_layer=" << process_layer_name << ".";
      return false;
    }
    conductors_.emplace(design_layer_id, conductor_layer);
  }

  return true;
}

const itf::LayerConductor* ResistanceCalc::ProcessLayerResolver::conductor(
    Size design_layer_id) const
{
  const auto it = conductors_.find(design_layer_id);
  return it == conductors_.end() ? nullptr : it->second;
}

const itf::LayerVia* ResistanceCalc::ProcessLayerResolver::via(
    Size design_layer_id) const
{
  const auto it = vias_.find(design_layer_id);
  return it == vias_.end() ? nullptr : it->second;
}

bool ResistanceCalc::validateInputs() const
{
  if (layout_data_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: LayoutData not initialized.";
    return false;
  }
  if (topo_pool_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: TopoPool not initialized.";
    return false;
  }
  if (layer_table_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: LayerTable not initialized.";
    return false;
  }
  if (corner_net_etch_pools_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: etch pools not set.";
    return false;
  }
  if (rc_table_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: RC table not set.";
    return false;
  }
  if (corner_data_ == nullptr || corner_data_->empty()) {
    LOG_ERROR << "calculate resistance failed: process corners not set.";
    return false;
  }
  for (const auto& corner : *corner_data_) {
    if (corner.process_corner == nullptr) {
      LOG_ERROR << "calculate resistance failed: null process corner "
                << corner.name << ".";
      return false;
    }
  }
  if (corner_net_etch_pools_->corner_num() != corner_data_->size()
      || corner_net_etch_pools_->net_num() != layout_data_->regular_net_count()) {
    LOG_ERROR << "calculate resistance failed: etch pool dimensions mismatch.";
    return false;
  }

  return true;
}

bool ResistanceCalc::buildCornerViews(std::vector<CornerCalcView>& views) const
{
  views.clear();
  views.reserve(corner_data_->size());

  for (Size corner_idx = 0; corner_idx < corner_data_->size(); ++corner_idx) {
    const auto& corner_data = (*corner_data_)[corner_idx];

    CornerCalcView view;
    view.idx = corner_idx;
    view.data = &corner_data;
    view.process_corner = corner_data.process_corner.get();
    view.temperature = corner_data.temperature;
    if (!view.layers.build(*layer_table_, *view.process_corner,
                           *topo_pool_, corner_data.name)) {
      return false;
    }

    views.push_back(std::move(view));
  }

  return true;
}

bool ResistanceCalc::calc()
{
  if (!validateInputs()) {
    return false;
  }

  std::vector<CornerCalcView> corner_views;
  if (!buildCornerViews(corner_views)) {
    return false;
  }

  for (const CornerCalcView& corner : corner_views) {
    calcCorner(corner);
  }

  return true;
}

void ResistanceCalc::calcCorner(const CornerCalcView& corner) const
{
  const Size regular_net_count = layout_data_->regular_net_count();

  #pragma omp parallel for schedule(dynamic)
  for (Size net_idx = 0; net_idx < regular_net_count; ++net_idx) {
    calcNet(corner, net_idx);
  }
}

void ResistanceCalc::calcNet(const CornerCalcView& corner, Size net_idx) const
{
  const CornerNetId corner_net_id{corner.idx, net_idx};
  const auto net_edges = topo_pool_->net_edges(net_idx);
  auto edge_resistances = rc_table_->corner_net_res_pool(corner_net_id);
  const EtchPool& etch_pool = corner_net_etch_pools_->at(corner_net_id);

  for (Size edge_idx = 0; edge_idx < net_edges.size(); ++edge_idx) {
    edge_resistances[edge_idx] =
        calcEdgeResistance(corner, net_idx, edge_idx, net_edges[edge_idx], etch_pool);
  }
}

F64 ResistanceCalc::calcEdgeResistance(const CornerCalcView& corner,
                                       Size net_idx,
                                       Size edge_idx,
                                       const TopoEdge& edge,
                                       const EtchPool& etch_pool) const
{
  if (edge.is_via()) {
    return calcViaResistance(corner, edge);
  }

  const auto edge_etch_intervals = etch_pool.edge_etch_interval_pool(edge_idx);
  if (edge_etch_intervals.empty()) {
    LOG_ERROR << "calculate resistance warning: no etch intervals, corner="
              << corner.data->name << ", net_idx=" << net_idx
              << ", edge_idx=" << edge_idx << ".";
    return 0.0;
  }

  return calcConductorResistance(corner, edge, edge_etch_intervals);
}

F64 ResistanceCalc::calcViaResistance(const CornerCalcView& corner,
                                      const TopoEdge& edge) const
{
  const itf::LayerVia* via_layer = corner.layers.via(edge.layer_id());
  if (via_layer == nullptr) {
    LOG_ERROR << "calculate resistance failed: via layer not bound, corner="
              << corner.data->name << ", design_layer_id=" << edge.layer_id()
              << ".";
    return 0.0;
  }

  F64 via_resistance = 0.0;
  const F64 via_area = geom::Area(edge.shape()) * dbu_to_micron_ * dbu_to_micron_;
  if (auto rpv = via_layer->get_rpv()) {
    via_resistance = rpv.value();
  } else {
    via_resistance = via_layer->query_rpv_vs_area(via_area);
  }

  return apply_via_temperature_derating(
      corner.temperature,
      *corner.process_corner,
      *via_layer,
      via_area,
      via_resistance);
}

F64 ResistanceCalc::calcConductorResistance(
    const CornerCalcView& corner,
    const TopoEdge& edge,
    std::span<const EtchInterval> edge_etch_intervals) const
{
  const itf::LayerConductor* conductor_layer = corner.layers.conductor(edge.layer_id());
  if (conductor_layer == nullptr) {
    LOG_ERROR << "calculate resistance failed: conductor layer not bound, corner="
              << corner.data->name << ", design_layer_id=" << edge.layer_id()
              << ".";
    return 0.0;
  }

  const LineSegment<Micron> segment = edgeSegment(edge);
  F64 resistance = 0.0;

  for (const EtchInterval& etch_interval : edge_etch_intervals) {
    const Micron overlap_lo = std::max(etch_interval.a0, segment.a0);
    const Micron overlap_hi = std::min(etch_interval.a1, segment.a1);
    if (overlap_hi <= overlap_lo) {
      continue;
    }

    const Micron overlap_length = overlap_hi - overlap_lo;
    const Micron thickness = etch_interval.thickness;
    const Micron width = etch_interval.width;
    LOG_ERROR_IF(width <= 0.0 || thickness <= 0.0)
        << "etch interval width/thickness <= 0.";

    float resistivity = 0.0;
    const auto rho_opt = conductor_layer->get_rho_v_siw_t().query_interpolation(
        static_cast<float>(thickness), static_cast<float>(width));
    if (rho_opt.has_value()) {
      resistivity = rho_opt.value();
    } else {
      resistivity = conductor_layer->get_rho();
    }

    float sheet_resistance = 0.0;
    if (resistivity <= 0.0) {
      const auto rpsq_opt = conductor_layer->get_rpsq_vs_si_width().query_interpolation(
          static_cast<float>(width));
      if (rpsq_opt.has_value()) {
        sheet_resistance = rpsq_opt.value();
      } else {
        sheet_resistance = conductor_layer->get_rpsq();
      }
    }

    F64 interval_resistance = 0.0;
    if (resistivity > 0.0) {
      interval_resistance += resistivity * overlap_length / (width * thickness);
    }
    if (sheet_resistance > 0.0) {
      interval_resistance += sheet_resistance * overlap_length / width;
    }

    resistance += apply_conductor_temperature_derating(
        corner.temperature,
        *corner.process_corner,
        *conductor_layer,
        width,
        interval_resistance);
  }

  return resistance;
}

LineSegment<Micron> ResistanceCalc::edgeSegment(const TopoEdge& edge) const
{
  const TopoNode& u_node = topo_pool_->node_at(edge.u());
  const TopoNode& v_node = topo_pool_->node_at(edge.v());

  LineSegment<Micron> segment;
  segment.is_horz = edge.is_horz();
  segment.fixed = edge.fixed() * dbu_to_micron_;
  if (edge.is_horz()) {
    segment.a0 = geom::X(u_node.point()) * dbu_to_micron_;
    segment.a1 = geom::X(v_node.point()) * dbu_to_micron_;
  } else {
    segment.a0 = geom::Y(u_node.point()) * dbu_to_micron_;
    segment.a1 = geom::Y(v_node.point()) * dbu_to_micron_;
  }
  if (segment.a1 < segment.a0) {
    std::swap(segment.a0, segment.a1);
  }
  return segment;
}

F64 ResistanceCalc::apply_conductor_temperature_derating(
    F64 operating_temperature,
    const itf::ProcessCorner& corner,
    const itf::LayerConductor& layer,
    Micron width,
    F64 base_resistance) const
{
  const TemperatureCoefficients coefficients =
      get_conductor_temperature_coefficients(layer, width);
  if (coefficients.crt1 == 0.0 && coefficients.crt2 == 0.0) {
    return base_resistance;
  }

  const F64 nominal_temperature = layer.has_t0()
                                      ? static_cast<F64>(layer.get_t0())
                                      : static_cast<F64>(corner.get_global_temperature());
  return applyResistanceTemperatureDerating(base_resistance, operating_temperature,
                                            nominal_temperature, coefficients.crt1,
                                            coefficients.crt2);
}

F64 ResistanceCalc::apply_via_temperature_derating(
    F64 operating_temperature,
    const itf::ProcessCorner& corner,
    const itf::LayerVia& layer,
    F64 area,
    F64 base_resistance) const
{
  const TemperatureCoefficients coefficients =
      get_via_temperature_coefficients(layer, area);
  if (coefficients.crt1 == 0.0 && coefficients.crt2 == 0.0) {
    return base_resistance;
  }

  const F64 nominal_temperature = layer.has_t0()
                                      ? static_cast<F64>(layer.get_t0())
                                      : static_cast<F64>(corner.get_global_temperature());
  return applyResistanceTemperatureDerating(base_resistance, operating_temperature,
                                            nominal_temperature, coefficients.crt1,
                                            coefficients.crt2);
}

}
