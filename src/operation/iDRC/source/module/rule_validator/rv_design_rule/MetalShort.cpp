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
#include "RuleValidator.hpp"

namespace idrc {

void RuleValidator::verifyMetalShort(RVCluster& rv_cluster)
{
  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_checking_polysets;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_net_total_rtrees;
  // preprocess for routing result and env
  {
    std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_total_polysets;
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge_total;
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge_checking;
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing()) {
        net_rects_to_merge_total[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing()) {
        net_rects_to_merge_total[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
        if (drc_shape->get_net_idx() != -1) {
          net_rects_to_merge_checking[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
        }
      }
    }
    for (auto& [layer_idx, net_map] : net_rects_to_merge_checking) {
      auto& target_net_map = routing_net_checking_polysets[layer_idx];
      for (auto& [net_idx, rect_vec] : net_map) {
        target_net_map[net_idx].insert(rect_vec.begin(), rect_vec.end());
      }
    }

    for (auto& [layer_idx, net_map] : net_rects_to_merge_total) {
      auto& target_net_map = routing_net_total_polysets[layer_idx];
      for (auto& [net_idx, rect_vec] : net_map) {
        target_net_map[net_idx].insert(rect_vec.begin(), rect_vec.end());
      }
    }

    for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_total_polysets) {
      std::vector<std::pair<BGRectInt, int32_t>> rtree_inputs;
      std::vector<GTLRectInt> rects;
      for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
        rects.clear();
        gtl::get_max_rectangles(rects, gtl_poly_set);
        for (GTLRectInt& gtl_rect : rects) {
          rtree_inputs.push_back({DRCUTIL.convertToBGRectInt(gtl_rect), net_idx});
        }
      }
      routing_net_total_rtrees[routing_layer_idx] = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>(rtree_inputs);
    }
  }

  // check rules
  for (auto& [routing_layer_idx, net_polyset] : routing_net_checking_polysets) {
    std::vector<Violation> layer_violations;
    for (auto& [net_idx, polyset] : net_polyset) {
      std::vector<GTLRectInt> rect_list;
      gtl::get_max_rectangles(rect_list, polyset);

      for (GTLRectInt& gtl_rect : rect_list) {
        PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
        std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_pair_list;
        {
          routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(rect)), std::back_inserter(bg_rect_net_pair_list));
        }
        for (auto& [bg_env_rect, env_net_idx] : bg_rect_net_pair_list) {
          if (net_idx == env_net_idx) {
            continue;
          }
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
          if (!DRCUTIL.isClosedOverlap(rect, env_rect)) {
            continue;
          }
          Violation violation;
          violation.set_violation_type(ViolationType::kMetalShort);
          violation.set_is_routing(true);
          violation.set_violation_net_set({net_idx, env_net_idx});
          violation.set_layer_idx(routing_layer_idx);
          violation.set_rect(DRCUTIL.getOverlap(rect, env_rect));
          violation.set_required_size(0);
          layer_violations.push_back(std::move(violation));
        }
      }
    }

    // postprocess, build final violations
    {
      if (layer_violations.size() > 1) {
        std::sort(layer_violations.begin(), layer_violations.end(), [](const Violation& a, const Violation& b) {
          const auto& ra = a.get_rect();
          const auto& rb = b.get_rect();
          if (ra.get_ll_x() != rb.get_ll_x())
            return ra.get_ll_x() < rb.get_ll_x();
          if (ra.get_ur_x() != rb.get_ur_x())
            return ra.get_ur_x() > rb.get_ur_x();
          if (ra.get_ll_y() != rb.get_ll_y())
            return ra.get_ll_y() < rb.get_ll_y();
          return ra.get_ur_y() > rb.get_ur_y();
        });

        std::vector<Violation> results;
        results.reserve(layer_violations.size());

        std::vector<const Violation*> active_set;

        for (const auto& v : layer_violations) {
          bool is_redundant = false;
          const auto& cur_r = v.get_rect();

          active_set.erase(
              std::remove_if(active_set.begin(), active_set.end(), [&](const Violation* p) { return p->get_rect().get_ur_x() < cur_r.get_ll_x(); }),
              active_set.end());

          for (const auto* p_active : active_set) {
            if (DRCUTIL.isInside(p_active->get_rect(), cur_r)) {
              is_redundant = true;
              break;
            }
          }

          if (!is_redundant) {
            results.push_back(v);
            active_set.push_back(&results.back());
          }
        }
        layer_violations = std::move(results);
      }

      rv_cluster.get_violation_list().insert(rv_cluster.get_violation_list().end(), std::make_move_iterator(layer_violations.begin()),
                                             std::make_move_iterator(layer_violations.end()));
    }
  }
}

}  // namespace idrc
