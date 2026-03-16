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
  const auto& layer_data = rv_cluster.get_layer_data();
  std::map<int32_t, bgi::rtree<GTLRectInt, bgi::quadratic<16>>> routing_env_rtree_map;
  for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
    if (!drc_shape->get_is_routing()) {
      continue;
    }
    routing_env_rtree_map[drc_shape->get_layer_idx()].insert(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
  }

  for (const auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    int32_t min_area = routing_layer_list[routing_layer_idx].get_minimum_area_rule().min_area;
    std::vector<Violation> layer_violations;
    std::vector<GTLPolyInt> layer_violation_poly_list;
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      if (net_idx == -1) {
        continue;
      }
      std::vector<GTLPolyInt> gtl_poly_list;
      routing_net.polyset.get_polygons(gtl_poly_list);
      for (GTLPolyInt& gtl_poly : gtl_poly_list) {
        if (gtl::area(gtl_poly) >= min_area) {
          continue;
        }
        GTLRectInt bbox;
        gtl::extents(bbox, gtl_poly);
        Violation violation;
        violation.set_violation_type(ViolationType::kMinimumArea);
        violation.set_is_routing(true);
        violation.set_violation_net_set({net_idx});
        violation.set_required_size(min_area);
        violation.set_layer_idx(routing_layer_idx);
        violation.set_rect(DRCUTIL.convertToPlanarRect(bbox));
        layer_violations.push_back(std::move(violation));
        layer_violation_poly_list.push_back(gtl_poly);
      }
    }

    // 依旧是对env做一次额外的检查，最后去掉env部分
    // 否则由于底层pin shape和cell blockage的net不同，需要引入效率低下的polyset合并
    auto env_rtree_it = routing_env_rtree_map.find(routing_layer_idx);
    if (env_rtree_it != routing_env_rtree_map.end()) {
      const auto& env_rtree = env_rtree_it->second;
      std::vector<Violation> filtered_violations;
      filtered_violations.reserve(layer_violations.size());
      for (size_t violation_idx = 0; violation_idx < layer_violations.size(); ++violation_idx) {
        Violation& violation = layer_violations[violation_idx];
        const GTLPolyInt& violation_poly = layer_violation_poly_list[violation_idx];
        GTLRectInt gtl_violation_bbox;
        gtl::extents(gtl_violation_bbox, violation_poly);
        std::vector<GTLRectInt> overlap_env_rect_list;
        env_rtree.query(bgi::intersects(gtl_violation_bbox), std::back_inserter(overlap_env_rect_list));
        if (overlap_env_rect_list.empty()) {
          filtered_violations.push_back(std::move(violation));
          continue;
        }

        const PlanarRect violation_rect = DRCUTIL.convertToPlanarRect(gtl_violation_bbox);
        bool fully_covered = false;
        for (const GTLRectInt& env_gtl_rect : overlap_env_rect_list) {
          if (DRCUTIL.isInside(DRCUTIL.convertToPlanarRect(env_gtl_rect), violation_rect)) {
            fully_covered = true;
            break;
          }
        }
        if (!fully_covered) {
          GTLPolySetInt violation_polyset;
          violation_polyset += violation_poly;
          GTLPolySetInt overlap_polyset;
          for (const GTLRectInt& env_gtl_rect : overlap_env_rect_list) {
            overlap_polyset += env_gtl_rect;
          }
          fully_covered = (gtl::area(overlap_polyset & violation_polyset) == gtl::area(violation_polyset));
        }
        if (fully_covered) {
          continue;
        }
        filtered_violations.push_back(std::move(violation));
      }
      layer_violations.swap(filtered_violations);
    }

    rv_cluster.get_violation_list().insert(rv_cluster.get_violation_list().end(), layer_violations.begin(), layer_violations.end());
  }
}

}  // namespace idrc
