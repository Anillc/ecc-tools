#include "CapacitanceCalc.hpp"

#include "CapTable.hpp"
#include "Environment.hpp"
#include "ProcessVariation.hpp"
#include "LayerTable.hpp"
#include "Types.hpp"
#include "log/Log.hh"

#include <algorithm>

namespace ircx {

void CapacitanceCalc::calc()
{
  ProcessVariation& pv = ProcessVariation::getOrCreateInst();
  Environment& env = Environment::getOrCreateInst();
  const Size corner_count = corner_num_;
  const Size net_count = net_num_;

  LOG_FATAL_IF(cap_tables_.size() != corner_num_)
      << "cap table count does not match corner count.";

  for (Size corner_idx = 0; corner_idx < corner_count; ++corner_idx) {
    const parser::CapTable* cap_table = cap_tables_[corner_idx];
    LOG_FATAL_IF(cap_table == nullptr)
        << "cap table missing for corner " << corners_[corner_idx]->get_technology();

    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
      const auto net_edges = topo_pool_->net_edges(net_idx);
      const Size edge_count = net_edges.size();
      auto edge_ground_caps = rc_table_->corner_net_gcap_pool(corner_idx, net_idx);

      const EtchPool& corner_net_etch_pool = pv.corner_net_etch_pool(corner_idx, net_idx);
      const EnvPool& net_env_pool = env.net_env_pool(net_idx);

      for (Size edge_idx = 0; edge_idx < edge_count; ++edge_idx) {
        const TopoEdge& edge = net_edges[edge_idx];
        if (edge.is_via()) continue;

        const Size process_layer_id = layer_table_->design_to_process_id(edge.layer_id());
        const Str& layer_name = layer_table_->process_name(process_layer_id);

        // Global index of current edge in TopoPool::edge_pool()
        const Size edge_global_id = topo_pool_->edge_index(edge);

        const auto env_intervals = net_env_pool.edge_env_interval_pool(edge_idx);
        const auto etch_intervals = corner_net_etch_pool.edge_etch_interval_pool(edge_idx);
        const Size interval_count = std::min(env_intervals.size(), etch_intervals.size());

        for (Size interval_idx = 0; interval_idx < interval_count; ++interval_idx) {
          const EnvInterval& env_interval = env_intervals[interval_idx];
          const EtchInterval& etch_interval = etch_intervals[interval_idx];

          const Dbu interval_lo_dbu = env_interval.a0;
          const Dbu interval_hi_dbu = env_interval.a1;
          if (interval_hi_dbu <= interval_lo_dbu) continue;

          auto queryNearCap = [&](const Str& belowLayer,
                                  const Str& aboveLayer,
                                  Micron spacing) {
            const Micron lookup_dist = std::max<Micron>(spacing, 0.0);
            if (aboveLayer.empty()) {
              return cap_table->queryTwoLayerCap(layer_name, belowLayer, lookup_dist);
            }
            return cap_table->queryThreeLayerCap(
                layer_name, belowLayer, aboveLayer, lookup_dist);
          };

          auto queryFarthestCap = [&](const Str& belowLayer,
                                      const Str& aboveLayer) {
            if (aboveLayer.empty()) {
              return cap_table->queryTwoLayerFarthestCap(layer_name, belowLayer);
            }
            return cap_table->queryThreeLayerFarthestCap(
                layer_name, belowLayer, aboveLayer);
          };

          struct SideContext {
            Micron spacing{0};
            const TopoEdge* adjacent{nullptr};
            bool occupied{false};
            bool regular{false};
            bool same_net{false};
          };

          auto buildSideContext = [&](Micron spacing, const TopoEdge* adjacent) {
            SideContext side;
            side.spacing = spacing;
            side.adjacent = adjacent;
            side.occupied = adjacent != nullptr;
            side.regular = side.occupied && adjacent->net_id() != kSpecialNetId;
            side.same_net = side.regular && adjacent->net_id() == net_idx;
            return side;
          };

          auto foldCoupling = [&](const SideContext& side, double coupling_cap_ff) {
            if (coupling_cap_ff <= 0.0 || !side.occupied) {
              return;
            }

            // Same-net and special-net coupling are modeled as ground capacitance.
            if (!side.regular || side.same_net) {
              edge_ground_caps[edge_idx] += coupling_cap_ff;
              return;
            }

            const Size adjacent_edge_global_id = topo_pool_->edge_index(*side.adjacent);
            rc_table_->append_net_ccap_entry(
                net_idx,
                edge_global_id,
                adjacent_edge_global_id,
                corner_idx,
                static_cast<F32>(coupling_cap_ff));
          };

          const SideContext low_side = buildSideContext(
              etch_interval.lo_spacing, env_interval.lo_adjacent);
          const SideContext high_side = buildSideContext(
              etch_interval.hi_spacing, env_interval.hi_adjacent);

          auto accumulateSpan = [&](Micron span_length,
                                    const Str& belowLayer,
                                    const Str& aboveLayer) {
            if (span_length <= 0) {
              return;
            }

            if (low_side.occupied && high_side.occupied) {
              // Two occupied sides:
              //   gcap = L * (cg_lo + cg_hi)
              //   ccap(side) = L * cc_side / 2
              const parser::CapacitanceResult low_side_cap =
                  queryNearCap(belowLayer, aboveLayer, low_side.spacing);
              const parser::CapacitanceResult high_side_cap =
                  queryNearCap(belowLayer, aboveLayer, high_side.spacing);

              edge_ground_caps[edge_idx] +=
                  span_length * (low_side_cap.ground_cap + high_side_cap.ground_cap);
              foldCoupling(low_side, span_length * low_side_cap.coupling_cap / 2.0);
              foldCoupling(high_side, span_length * high_side_cap.coupling_cap / 2.0);
            } else if (low_side.occupied || high_side.occupied) {
              // One occupied side:
              //   gcap = 2 * L * cg
              //   ccap = L * cc
              const SideContext& occupied_side = low_side.occupied ? low_side : high_side;
              const parser::CapacitanceResult occupied_side_cap =
                  queryNearCap(belowLayer, aboveLayer, occupied_side.spacing);

              edge_ground_caps[edge_idx] += 2.0 * span_length * occupied_side_cap.ground_cap;
              foldCoupling(occupied_side, span_length * occupied_side_cap.coupling_cap);
            } else {
              // No occupied sides:
              //   take the farthest table row and use
              //   gcap = 2 * L * (cg + cc)
              const parser::CapacitanceResult far_cap =
                  queryFarthestCap(belowLayer, aboveLayer);
              edge_ground_caps[edge_idx] +=
                  2.0 * span_length * (far_cap.ground_cap + far_cap.coupling_cap);
            }
          };

          Dbu cursor_dbu = interval_lo_dbu;
          for (const CrossOverlapSub& cross_overlap : env_interval.cross_segs) {
            if (cursor_dbu < cross_overlap.a0) {
              Str below_layer;
              Str above_layer;
              resolveCrossLayers(nullptr, below_layer, above_layer);
              accumulateSpan(
                  (cross_overlap.a0 - cursor_dbu) * dbu_to_micron_,
                  below_layer,
                  above_layer);
              cursor_dbu = cross_overlap.a0;
            }

            const Dbu overlap_hi_dbu = std::min(interval_hi_dbu, cross_overlap.a1);
            if (cursor_dbu < overlap_hi_dbu) {
              Str below_layer;
              Str above_layer;
              resolveCrossLayers(&cross_overlap, below_layer, above_layer);
              accumulateSpan((overlap_hi_dbu - cursor_dbu) * dbu_to_micron_, below_layer, above_layer);
              cursor_dbu = overlap_hi_dbu;
            }
          }

          if (cursor_dbu < interval_hi_dbu) {
            Str below_layer;
            Str above_layer;
            resolveCrossLayers(nullptr, below_layer, above_layer);
            accumulateSpan((interval_hi_dbu - cursor_dbu) * dbu_to_micron_, below_layer, above_layer);
          }
        }
      }

      // results already written into pre-allocated span
    }
  }

  // merge per-net coupling cap entries (sequential)
  rc_table_->merge_net_ccap_entries();
}

void CapacitanceCalc::resolveCrossLayers(
    const CrossOverlapSub* crossSeg,
    Str& belowLayer,
    Str& aboveLayer) const
{
  belowLayer = "SUBSTRATE";
  aboveLayer.clear();

  if (crossSeg == nullptr) {
    return;
  }

  if (crossSeg->blw_layer != 0) {
    Size procId = layer_table_->design_to_process_id(crossSeg->blw_layer);
    belowLayer = layer_table_->process_name(procId);
  }
  if (crossSeg->abv_layer != 0) {
    Size procId = layer_table_->design_to_process_id(crossSeg->abv_layer);
    aboveLayer = layer_table_->process_name(procId);
  }
}

}  // namespace ircx
