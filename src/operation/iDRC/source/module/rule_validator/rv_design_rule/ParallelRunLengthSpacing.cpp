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

void RuleValidator::verifyParallelRunLengthSpacing(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();

  std::map<int32_t, std::map<int32_t, std::vector<PlanarRect>>> routing_net_rect_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_net_total_rtrees;
  // preprocess, get routing result and env
  {
    std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_total_polysets;
    std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_checking_polysets;
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge_total;
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge_checking;
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing()) {
        net_rects_to_merge_total[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
        if (drc_shape->get_net_idx() != -1) {
          net_rects_to_merge_checking[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
        }
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
    auto merge_to_polyset = [](auto& src, auto& dst) {
      for (auto& [layer_idx, net_map] : src) {
        auto& target_net_map = dst[layer_idx];
        for (auto& [net_idx, rect_vec] : net_map) {
          target_net_map[net_idx].insert(rect_vec.begin(), rect_vec.end());
        }
      }
    };

    merge_to_polyset(net_rects_to_merge_checking, routing_net_checking_polysets);
    merge_to_polyset(net_rects_to_merge_total, routing_net_total_polysets);

    for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_checking_polysets) {
      std::vector<GTLRectInt> rects;
      auto& layer_net_rects = routing_net_rect_map[routing_layer_idx];
      for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
        rects.clear();
        gtl::get_max_rectangles(rects, gtl_poly_set);
        for (auto& rect : rects) {
          layer_net_rects[net_idx].push_back(DRCUTIL.convertToPlanarRect(rect));
        }
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

  for (auto& [routing_layer_idx, net_rect_map] : routing_net_rect_map) {
    RoutingLayer& routing_layer = routing_layer_list[routing_layer_idx];
    ParallelRunLengthSpacingRule& parallel_run_length_spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> net_required_violation_rect_map;
    std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_pair_list;
    std::vector<PlanarRect> violation_env_rect_list;
    for (auto& [net_idx, rect_list] : net_rect_map) {
      for (PlanarRect& rect : rect_list) {
        bg_rect_net_pair_list.clear();
        violation_env_rect_list.clear();
        {
          int32_t width_max_spacing = parallel_run_length_spacing_rule.getSpacing(rect.getWidth(), rect.getLength());
          PlanarRect check_rect = DRCUTIL.getEnlargedRect(rect, width_max_spacing);
          routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)), std::back_inserter(bg_rect_net_pair_list));
          for (auto& [bg_rect, net] : bg_rect_net_pair_list) {
            violation_env_rect_list.push_back(DRCUTIL.convertToPlanarRect(bg_rect));
          }
        }
        for (auto& [bg_env_rect, env_net_idx] : bg_rect_net_pair_list) {
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
          if (DRCUTIL.isClosedOverlap(rect, env_rect)) {
            continue;
          }
          // prl with overlaped shapes
          int32_t prl = DRCUTIL.getParallelLength(rect, env_rect);
          int32_t required_size = parallel_run_length_spacing_rule.getSpacing(std::max(rect.getWidth(), env_rect.getWidth()), prl);
          int32_t spacing = DRCUTIL.getEuclideanDistance(env_rect, rect);
          if (required_size <= spacing) {
            continue;
          }

          PlanarRect violation_rect = DRCUTIL.getSpacingRect(rect, env_rect);
          if (prl < 0 && env_net_idx == -1) {
            continue;
          }

          if (prl > 0 || (prl < 0 && env_net_idx == net_idx)) {
            GTLPolySetInt vioaltion_around_set;
            for (auto& [bg_env_rect, env_net_idx] : bg_rect_net_pair_list) {
              PlanarRect violation_env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
              if (DRCUTIL.isOpenOverlap(violation_rect, violation_env_rect)) {
                vioaltion_around_set += DRCUTIL.convertToGTLRectInt(violation_env_rect);
              }
            }
            if (!gtl::empty(vioaltion_around_set)) {
              GTLRectInt min_violation;
              gtl::extents(min_violation, DRCUTIL.convertToGTLRectInt(violation_rect) - vioaltion_around_set);
              violation_rect = DRCUTIL.convertToPlanarRect(min_violation);
            }
            if (violation_rect.getArea() == 0) {
              continue;
            }
          }

          // exact prl
          if (prl > 0) {
            prl = DRCUTIL.getParallelLength(violation_rect, rect);
            required_size = parallel_run_length_spacing_rule.getSpacing(std::max(rect.getWidth(), env_rect.getWidth()), prl);
            if (required_size <= spacing) {
              continue;
            }
          }

          bool is_zero_area = (violation_rect.getArea() == 0);
          bool is_horizontal_inside = false, is_vertical_inside = false;
          bool valid_violation = true;
          if (DRCUTIL.isInside(rect, violation_rect)) {
            continue;
          }

          for (PlanarRect& violation_env_rect : violation_env_rect_list) {
            if (!DRCUTIL.isClosedOverlap(violation_env_rect, violation_rect)) {
              continue;
            }
            if (violation_env_rect == env_rect || violation_env_rect == rect) {
              continue;
            }

            if (is_zero_area) {
              if (DRCUTIL.isInside(violation_env_rect, violation_rect) && DRCUTIL.isInside(violation_env_rect, violation_rect.getMidPoint(), false)) {
                valid_violation = false;
                break;
              }
              if (net_idx == env_net_idx && (DRCUTIL.isInside(violation_env_rect, violation_rect))) {
                valid_violation = false;
                break;
              }
            } else if (prl < 0 && env_net_idx == net_idx) {
              if (!is_horizontal_inside
                  && (DRCUTIL.isInside(violation_env_rect, DRCUTIL.getRect(violation_rect.getOrientEdge(Orientation::kWest)))
                      || DRCUTIL.isInside(violation_env_rect, DRCUTIL.getRect(violation_rect.getOrientEdge(Orientation::kEast))))) {
                is_horizontal_inside = true;
              }
              if (!is_vertical_inside
                  && (DRCUTIL.isInside(violation_env_rect, DRCUTIL.getRect(violation_rect.getOrientEdge(Orientation::kSouth)))
                      || DRCUTIL.isInside(violation_env_rect, DRCUTIL.getRect(violation_rect.getOrientEdge(Orientation::kNorth))))) {
                is_vertical_inside = true;
              }
            }
          }
          if (!valid_violation) {
            continue;
          }
          if (!is_zero_area && is_horizontal_inside && is_vertical_inside) {
            continue;
          }
          net_required_violation_rect_map[{net_idx, env_net_idx}][required_size].push_back(violation_rect);
        }
      }
    }
    std::map<PlanarRect, bool, CmpPlanarRectByXASC> rect_computed;
    for (auto& [violation_net_set, required_violation_rect_map] : net_required_violation_rect_map) {
      for (auto& [required_size, violation_rect_list] : required_violation_rect_map) {
        for (PlanarRect& violation_rect : violation_rect_list) {
          bool is_inside = false;
          for (PlanarRect& other_violation_rect : violation_rect_list) {
            if (other_violation_rect == violation_rect) {
              continue;
            }
            if (DRCUTIL.isInside(other_violation_rect, violation_rect)) {
              is_inside = true;
              break;
            }
          }
          if (!is_inside) {
            Violation violation;
            violation.set_violation_type(ViolationType::kParallelRunLengthSpacing);
            violation.set_is_routing(true);
            violation.set_violation_net_set(violation_net_set);
            violation.set_layer_idx(routing_layer_idx);
            violation.set_rect(violation_rect);
            violation.set_required_size(required_size);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
}

}  // namespace idrc
