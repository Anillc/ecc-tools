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
#include "ViaResistanceModel.hh"

#include "Geometry.hh"
#include "ProcessCorner.hpp"
#include "ResistanceTemperature.hh"
#include "TopoPool.hh"

namespace ircx {

auto ViaResistanceModel::calc(const TopoEdge& edge, const itf::ProcessCorner& corner, const itf::LayerVia& layer, Micron micron_per_dbu,
                              F64 operating_temperature) -> F64
{
  const F64 via_area = geom::area(edge.shape()) * micron_per_dbu * micron_per_dbu;

  F64 via_resistance = 0.0;
  if (auto rpv = layer.get_rpv()) {
    via_resistance = rpv.value();
  } else {
    via_resistance = layer.query_rpv_vs_area(via_area);
  }

  const ResistanceTemperatureCoefficients coefficients =
      resistanceTemperatureCoefficients(layer, [&](auto& crt1, auto& crt2) {
        layer.query_crt_vs_area(via_area, crt1, crt2);
      });
  return applyResistanceTemperatureDerating(via_resistance, operating_temperature, resistanceNominalTemperature(layer, corner),
                                            coefficients);
}

}  // namespace ircx
