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

void RuleValidator::verifyMinimumArea(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();

  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_gtl_poly_set_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_net_rtrees;

  // preprocess for routing result
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
      auto& target_net_map = routing_net_gtl_poly_set_map[layer_idx];
      for (auto& [net_idx, rect_vec] : net_map) {
        target_net_map[net_idx].insert(rect_vec.begin(), rect_vec.end());
      }
    }

    for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_gtl_poly_set_map) {
      std::vector<std::pair<BGRectInt, int32_t>> rtree_inputs;
      std::vector<GTLRectInt> rects;
      for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
        rects.clear();
        gtl::get_max_rectangles(rects, gtl_poly_set);
        for (GTLRectInt& gtl_rect : rects) {
          rtree_inputs.push_back({DRCUTIL.convertToBGRectInt(gtl_rect), net_idx});
        }
      }
      routing_net_rtrees[routing_layer_idx] = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>(rtree_inputs);
    }
  }

  // preprocess for env
  {
    std::map<int32_t, std::vector<GTLRectInt>> env_rects_to_merge;
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing() && drc_shape->get_net_idx() == -1) {
        env_rects_to_merge[drc_shape->get_layer_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing() && drc_shape->get_net_idx() == -1) {
        env_rects_to_merge[drc_shape->get_layer_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
      }
    }

    std::map<int32_t, GTLPolySetInt> routing_env_polyset;
    for (auto& [layer_idx, rect_vec] : env_rects_to_merge) {
      routing_env_polyset[layer_idx].insert(rect_vec.begin(), rect_vec.end());
    }

    for (auto& [routing_layer_idx, polyset] : routing_env_polyset) {
      const auto& rtree = routing_net_rtrees[routing_layer_idx];
      auto& layer_net_map = routing_net_gtl_poly_set_map[routing_layer_idx];

      std::vector<GTLPolyInt> env_polygons;
      polyset.get_polygons(env_polygons);

      std::vector<GTLRectInt> env_rects;
      for (GTLPolyInt& polygon : env_polygons) {
        env_rects.clear();
        gtl::get_max_rectangles(env_rects, polygon);
        for (GTLRectInt& gtl_env_rect : env_rects) {
          // std::vector<std::pair<BGRectInt, int32_t>> overlaped_pairs;
          auto it = rtree.qbegin(bgi::intersects(DRCUTIL.convertToBGRectInt(gtl_env_rect)));
          if (it != rtree.qend()) {
            int32_t net = it->second;
            layer_net_map[net] += polygon;
            break;
          }
        }
      }
    }
  }

  // check each polygon
  for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_gtl_poly_set_map) {
    int32_t min_area = routing_layer_list[routing_layer_idx].get_minimum_area_rule().min_area;
    std::vector<Violation> layer_violations;
    for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
      std::vector<GTLPolyInt> gtl_poly_list;
      gtl_poly_set.get_polygons(gtl_poly_list);
      for (GTLPolyInt& gtl_poly : gtl_poly_list) {
        if (gtl::area(gtl_poly) >= min_area) {
          continue;
        }
        std::vector<GTLRectInt> gtl_rect_list;
        gtl::get_rectangles(gtl_rect_list, gtl_poly, gtl::HORIZONTAL);

        GTLRectInt best_gtl_rect;
        int32_t max_xh = std::numeric_limits<int32_t>::min();
        int32_t max_x_span = std::numeric_limits<int32_t>::min();
        int32_t max_yh = std::numeric_limits<int32_t>::min();
        bool first = true;

        for (const GTLRectInt& gtl_rect : gtl_rect_list) {
          int32_t curr_xh = gtl::xh(gtl_rect);
          int32_t curr_x_span = gtl::xh(gtl_rect) - gtl::xl(gtl_rect);  // x span
          int32_t curr_yh = gtl::yh(gtl_rect);

          bool update = false;
          if (first) {
            update = true;
          } else if (curr_xh > max_xh) {
            update = true;
          } else if (curr_xh == max_xh) {
            if (curr_x_span > max_x_span) {
              update = true;
            } else if (curr_x_span == max_x_span) {
              if (curr_yh > max_yh) {
                update = true;
              }
            }
          }

          if (update) {
            max_xh = curr_xh;
            max_x_span = curr_x_span;
            max_yh = curr_yh;
            best_gtl_rect = gtl_rect;
            first = false;
          }
        }
        Violation violation;
        violation.set_violation_type(ViolationType::kMinimumArea);
        violation.set_is_routing(true);
        violation.set_violation_net_set({net_idx});
        violation.set_required_size(min_area);
        violation.set_layer_idx(routing_layer_idx);
        violation.set_rect(DRCUTIL.convertToPlanarRect(best_gtl_rect));
        layer_violations.push_back(std::move(violation));
      }
    }

    // postprocess, build final violations
    {
      if (layer_violations.size() > 1) {
        auto update_net_func = [](Violation& master, const Violation& slave) {
          const auto& rm = master.get_rect();
          const auto& rs = slave.get_rect();
          if (rs.get_ur_x() > rm.get_ur_x() || (rs.get_ur_x() == rm.get_ur_x() && rs.get_ur_y() > rm.get_ur_y())) {
            master.set_violation_net_set(slave.get_violation_net_set());
          }
        };

        std::sort(layer_violations.begin(), layer_violations.end(),
                  [](const Violation& a, const Violation& b) { return CmpPlanarRectByYASC()(a.get_rect(), b.get_rect()); });

        int write_idx = 0;
        int32_t unit_w = layer_violations[0].get_rect().getWidth();
        int32_t unit_h = layer_violations[0].get_rect().getLength();

        for (size_t i = 1; i < layer_violations.size(); ++i) {
          auto& master = layer_violations[write_idx];
          const auto& slave = layer_violations[i];
          const auto& rm = master.get_rect();
          const auto& rs = slave.get_rect();

          if (DRCUTIL.isInside(rm, rs)) {
            continue;
          }
          if (DRCUTIL.isInside(rs, rm)) {
            master = slave;
            unit_w = rs.getWidth();
            unit_h = rs.getLength();
            continue;
          }

          bool same_height = (rm.get_ll_y() == rs.get_ll_y() && rm.get_ur_y() == rs.get_ur_y());
          bool overlap_h = (rm.get_ur_x() > rs.get_ll_x());
          bool is_unit_size = (rs.getWidth() == unit_w && rs.getLength() == unit_h);

          if (same_height && overlap_h && is_unit_size) {
            update_net_func(master, slave);
            int32_t new_ur_x = std::max(rm.get_ur_x(), rs.get_ur_x());
            master.set_rect(PlanarRect(rm.get_ll_x(), rm.get_ll_y(), new_ur_x, rm.get_ur_y()));
          } else {
            layer_violations[++write_idx] = slave;
            unit_w = slave.get_rect().getWidth();
            unit_h = slave.get_rect().getLength();
          }
        }
        layer_violations.resize(write_idx + 1);

        std::sort(layer_violations.begin(), layer_violations.end(),
                  [](const Violation& a, const Violation& b) { return CmpPlanarRectByXASC()(a.get_rect(), b.get_rect()); });

        write_idx = 0;
        unit_w = layer_violations[0].get_rect().getWidth();
        unit_h = layer_violations[0].get_rect().getLength();

        for (size_t i = 1; i < layer_violations.size(); ++i) {
          auto& master = layer_violations[write_idx];
          const auto& slave = layer_violations[i];
          const auto& rm = master.get_rect();
          const auto& rs = slave.get_rect();

          if (DRCUTIL.isInside(rm, rs)) {
            continue;
          }
          if (DRCUTIL.isInside(rs, rm)) {
            master = slave;
            unit_w = rs.getWidth();
            unit_h = rs.getLength();
            continue;
          }

          bool same_width = (rm.get_ll_x() == rs.get_ll_x() && rm.get_ur_x() == rs.get_ur_x());
          bool overlap_v = (rm.get_ur_y() > rs.get_ll_y());
          bool is_unit_size = (rs.getWidth() == unit_w && rs.getLength() == unit_h);

          if (same_width && overlap_v && is_unit_size) {
            update_net_func(master, slave);
            int32_t new_ur_y = std::max(rm.get_ur_y(), rs.get_ur_y());
            master.set_rect(PlanarRect(rm.get_ll_x(), rm.get_ll_y(), rm.get_ur_x(), new_ur_y));
          } else {
            layer_violations[++write_idx] = slave;
            unit_w = slave.get_rect().getWidth();
            unit_h = slave.get_rect().getLength();
          }
        }
        layer_violations.resize(write_idx + 1);
      }

      rv_cluster.get_violation_list().insert(rv_cluster.get_violation_list().end(), layer_violations.begin(), layer_violations.end());
    }
  }
}

}  // namespace idrc
