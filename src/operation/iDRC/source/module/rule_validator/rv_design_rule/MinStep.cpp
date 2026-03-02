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

void RuleValidator::verifyMinStep(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_checking_polysets;

  // preprocess for routing result and env
  {
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge;
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        net_rects_to_merge[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        net_rects_to_merge[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
      }
    }
    for (auto& [layer_idx, net_map] : net_rects_to_merge) {
      auto& target_net_map = routing_net_checking_polysets[layer_idx];
      for (auto& [net_idx, rect_vec] : net_map) {
        target_net_map[net_idx].insert(rect_vec.begin(), rect_vec.end());
      }
    }
  }

  for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_checking_polysets) {
    std::vector<Violation> layer_violations;
    RoutingLayer& routing_layer = routing_layer_list[routing_layer_idx];
    MinStepRule& min_step_rule = routing_layer.get_min_step_rule();
    int32_t min_step = min_step_rule.min_step;
    int32_t max_edges = min_step_rule.max_edges;
    int32_t lef58_min_step = min_step_rule.lef58_min_step;
    int32_t lef58_min_adjacent_length = min_step_rule.lef58_min_adjacent_length;

    for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
      std::vector<GTLHolePolyInt> gtl_hole_poly_list;
      gtl_poly_set.get(gtl_hole_poly_list);
      for (GTLHolePolyInt& gtl_hole_poly : gtl_hole_poly_list) {
        std::vector<std::pair<GTLHolePolyInt, bool>> check_hole_pair_list;
        {
          check_hole_pair_list.emplace_back(gtl_hole_poly, false);
          for (auto iter = gtl_hole_poly.begin_holes(); iter != gtl_hole_poly.end_holes(); iter++) {
            GTLPolyInt gtl_poly = *iter;
            GTLHolePolyInt check_hole_poly;
            check_hole_poly.set(gtl_poly.begin(), gtl_poly.end());
            check_hole_pair_list.emplace_back(check_hole_poly, true);
          }
        }

        for (auto& [check_hole_poly, is_hole] : check_hole_pair_list) {
          int32_t coord_size = static_cast<int32_t>(check_hole_poly.size());
          if (coord_size < 4) {
            continue;
          }

          std::vector<PlanarCoord> coord_list;
          coord_list.reserve(coord_size);
          std::vector<int32_t> edge_length_list;
          edge_length_list.reserve(coord_size);
          std::vector<bool> convex_corner_list;
          convex_corner_list.reserve(coord_size);

          for (auto iter = check_hole_poly.begin(); iter != check_hole_poly.end(); iter++) {
            coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
          }

          for (int32_t i = 0; i < coord_size; i++) {
            PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
            PlanarCoord& curr_coord = coord_list[i];
            PlanarCoord& post_coord = coord_list[getIdx(i + 1, coord_size)];
            edge_length_list.push_back(DRCUTIL.getManhattanDistance(pre_coord, curr_coord));
            if (is_hole) {
              convex_corner_list.push_back(DRCUTIL.isConcaveCorner(DRCUTIL.getRotation(check_hole_poly), pre_coord, curr_coord, post_coord));
            } else {
              convex_corner_list.push_back(DRCUTIL.isConvexCorner(DRCUTIL.getRotation(check_hole_poly), pre_coord, curr_coord, post_coord));
            }
          }

          for (int32_t i = 0; i < coord_size; i++) {
            // case 1
            if (edge_length_list[i] < min_step) {
              int32_t small_edge_num = 1;
              for (int32_t j = 1; j < coord_size; ++j) {
                if (min_step <= edge_length_list[getIdx(i + j, coord_size)]) {
                  break;
                }
                small_edge_num++;
              }
              if (max_edges < small_edge_num) {
                PlanarRect violation_rect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
                int32_t total_steps = small_edge_num + 1;

                for (int32_t step = 0; step < total_steps; step++) {
                  int32_t idx = getIdx(i - 1 + step, coord_size);
                  violation_rect.set_ll_x(std::min(violation_rect.get_ll_x(), coord_list[idx].get_x()));
                  violation_rect.set_ll_y(std::min(violation_rect.get_ll_y(), coord_list[idx].get_y()));
                  violation_rect.set_ur_x(std::max(violation_rect.get_ur_x(), coord_list[idx].get_x()));
                  violation_rect.set_ur_y(std::max(violation_rect.get_ur_y(), coord_list[idx].get_y()));
                }

                Violation violation;
                violation.set_violation_type(ViolationType::kMinStep);
                violation.set_required_size(min_step);
                violation.set_is_routing(true);
                violation.set_violation_net_set({net_idx});
                violation.set_layer_idx(routing_layer_idx);
                violation.set_rect(violation_rect);
                rv_cluster.get_violation_list().push_back(violation);
              }
              i += small_edge_num - 1;
            }
          }

          for (int32_t i = 0; i < coord_size; i++) {
            // case 2
            int32_t pre_i = getIdx(i - 1, coord_size);
            int32_t post_i = getIdx(i + 1, coord_size);
            if (convex_corner_list[pre_i] == false && convex_corner_list[i] == true && convex_corner_list[post_i] == false) {
              if ((edge_length_list[i] < lef58_min_step && edge_length_list[post_i] < lef58_min_adjacent_length)
                  || (edge_length_list[i] < lef58_min_adjacent_length && edge_length_list[post_i] < lef58_min_step)) {
                PlanarRect violation_rect = DRCUTIL.getRect(coord_list[pre_i], coord_list[post_i]);
                Violation violation;
                violation.set_violation_type(ViolationType::kMinStep);
                violation.set_required_size(lef58_min_step);
                violation.set_is_routing(true);
                violation.set_violation_net_set({net_idx});
                violation.set_layer_idx(routing_layer_idx);
                violation.set_rect(violation_rect);
                rv_cluster.get_violation_list().push_back(violation);
              }
            }
          }
        }
      }
    }
  }
}
}  // namespace idrc
