#include "ResistanceCalc.hpp"

#include "LayoutData.hpp"
#include "TopoPool.hpp"
#include "Environment.hpp"
#include "ProcessVariation.hpp"
#include "LayerTable.hpp"
#include "Types.hpp"

namespace ircx {

void ResistanceCalc::calc()
{
  ProcessVariation& pv = ProcessVariation::getOrCreateInst();
  const Size regular_net_count = layout_data_->regular_net_count();
  const Size corner_count = corners_.size();

  for (Size corner_idx = 0; corner_idx < corner_count; corner_idx++) {
    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < regular_net_count; net_idx++) {
      const auto net_edges = topo_pool_->net_edges(net_idx);
      const Size edge_count = net_edges.size();
      auto edge_resistances = rc_table_->corner_net_res_pool(corner_idx, net_idx);
      const EtchPool& corner_net_etch_pool = pv.corner_net_etch_pool(corner_idx, net_idx);
      for (Size edge_idx = 0; edge_idx < edge_count; edge_idx++) {
        const TopoEdge& edge = net_edges[edge_idx];
        const Size process_layer_id = layer_table_->design_to_process_id(edge.layer_id());

        if (edge.is_via()) {
          auto* via_layer = corners_[corner_idx]->get_layers()->find_via_layer(process_layer_id);

          if (auto rpv = via_layer->get_rpv()) {
            edge_resistances[edge_idx] = rpv.value();
          } else {
            edge_resistances[edge_idx] = via_layer->query_rpv_vs_area(geom::Area(edge.shape()));
          }
          continue; // via res
        }

        auto* conductor_layer = corners_[corner_idx]->get_layers()->find_conductor_layer(process_layer_id);

        auto [segment_lo, segment_hi] = node_range(edge);
        const auto edge_etch_intervals = corner_net_etch_pool.edge_etch_interval_pool(edge_idx);
        F64 resistance = 0.0;
        for (const EtchInterval& etch_interval : edge_etch_intervals) {
          // Overlap of this etch interval with the u->v segment.
          const Micron overlap_lo = std::max(etch_interval.a0, segment_lo);
          const Micron overlap_hi = std::min(etch_interval.a1, segment_hi);
          if (overlap_hi <= overlap_lo) continue;

          const Micron overlap_length = overlap_hi - overlap_lo;

          // Modeled thickness: use value from ThicknessModel; fall back to nominal.
          const Micron thickness = etch_interval.thickness;
          const Micron width = etch_interval.width;
          LOG_ERROR_IF(width <= 0.0 || thickness <= 0.0) << "etch interval width/thickness <= 0.";

          // ρ (Ω·μm): look up from RHO_VS_SI_WIDTH_AND_THICKNESS (row=T, col=W).
          float resistivity = 0.0;
          {
            auto rho_opt = conductor_layer->get_rho_v_siw_t().query_interpolation(
                static_cast<float>(thickness), static_cast<float>(width));
            if (rho_opt.has_value()) {
              resistivity = rho_opt.value();
            } else {
              resistivity = conductor_layer->get_rho();
            }
          }

          float sheet_resistance = 0.0;
          if (resistivity <= 0.0) {
            // RPSQ path (sheet resistance): R = Rₛ × L / W
            // Used when the conductor layer only specifies RPSQ (e.g. poly, diffusion).
            auto rpsq_opt = conductor_layer->get_rpsq_vs_si_width().query_interpolation(
                static_cast<float>(width));
            if (rpsq_opt.has_value()) {
              sheet_resistance = rpsq_opt.value();
            } else {
              sheet_resistance = conductor_layer->get_rpsq();
            }
          }

          if (resistivity > 0.0) {
            resistance += resistivity * overlap_length / (width * thickness);
          }
          if (sheet_resistance > 0.0) {
            resistance += sheet_resistance * overlap_length / width;
          }
        }
        edge_resistances[edge_idx] = resistance;
      }
      // results already written into pre-allocated span
    }
  }
}

std::pair<Micron, Micron> ResistanceCalc::node_range(const TopoEdge& e) const
{
  const TopoNode& u_node = topo_pool_->node_at(e.u());
  const TopoNode& v_node = topo_pool_->node_at(e.v());

  Micron u_pos, v_pos;
  if (e.is_horz()) {
    u_pos = geom::X(u_node.point()) * dbu_to_micron_;
    v_pos = geom::X(v_node.point()) * dbu_to_micron_;
  } else {
    u_pos = geom::Y(u_node.point()) * dbu_to_micron_;
    v_pos = geom::Y(v_node.point()) * dbu_to_micron_;
  }
  return {u_pos, v_pos};
}


}
