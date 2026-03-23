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

void RuleValidator::verifyEnclosureParallel(RVCluster& rv_cluster)
{
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();
  const auto& layer_data = rv_cluster.get_layer_data();
  auto is_eol_boundary = [](const RVLayerData& rv_layer_data, int32_t boundary_id, int32_t eol_width) {
    const BoundaryData& curr_boundary = rv_layer_data.getBoundary(boundary_id);
    if (curr_boundary.edge_length >= eol_width) {
      return false;
    }
    const BoundaryData& prev_boundary = rv_layer_data.getPrevBoundary(boundary_id);
    return prev_boundary.isConvex && curr_boundary.isConvex;
  };
  auto build_parallel_windows = [this](const BoundaryData& boundary_data, const EnclosureParallelRule& curr_rule, PlanarRect& pre_rect,
                                       PlanarRect& post_rect) {
    int32_t par_spacing = curr_rule.par_spacing - boundary_data.edge_length;
    switch (boundary_data.orient) {
      case Orientation::kEast:
        pre_rect = DRCUTIL.getEnlargedRect(boundary_data.begin_coord, curr_rule.backward_ext, par_spacing, curr_rule.forward_ext, 0);
        post_rect = DRCUTIL.getEnlargedRect(boundary_data.end_coord, curr_rule.backward_ext, 0, curr_rule.forward_ext, par_spacing);
        break;
      case Orientation::kWest:
        pre_rect = DRCUTIL.getEnlargedRect(boundary_data.begin_coord, curr_rule.forward_ext, 0, curr_rule.backward_ext, par_spacing);
        post_rect = DRCUTIL.getEnlargedRect(boundary_data.end_coord, curr_rule.forward_ext, par_spacing, curr_rule.backward_ext, 0);
        break;
      case Orientation::kSouth:
        pre_rect = DRCUTIL.getEnlargedRect(boundary_data.begin_coord, par_spacing, curr_rule.forward_ext, 0, curr_rule.backward_ext);
        post_rect = DRCUTIL.getEnlargedRect(boundary_data.end_coord, 0, curr_rule.forward_ext, par_spacing, curr_rule.backward_ext);
        break;
      case Orientation::kNorth:
        pre_rect = DRCUTIL.getEnlargedRect(boundary_data.begin_coord, 0, curr_rule.backward_ext, par_spacing, curr_rule.forward_ext);
        post_rect = DRCUTIL.getEnlargedRect(boundary_data.end_coord, par_spacing, curr_rule.backward_ext, 0, curr_rule.forward_ext);
        break;
      default:
        DRCLOG.error(Loc::current(), "The orientation is error!");
    }
  };
  for (const auto& [cut_layer_idx, cut_layer_data] : layer_data) {
    if (cut_layer_data.cut_pool.empty()) {
      continue;
    }
    const std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
    if (routing_layer_idx_list.size() < 2) {
      continue;
    }
    int32_t above_routing_layer_idx = *std::max_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    int32_t below_routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    const EnclosureParallelRule& curr_rule = cut_layer_list[cut_layer_idx].get_enclosure_parallel_rule();
    for (const CutData& cut_data : cut_layer_data.getCuts()) {
      GTLRectInt cut_gtl_rect = cut_data.rect;
      int32_t cut_net_idx = cut_data.net_idx;
      if (cut_net_idx == -1) {
        continue;
      }
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_gtl_rect);
      for (int32_t routing_layer_idx : routing_layer_idx_list) {
        if (curr_rule.has_above && (routing_layer_idx != above_routing_layer_idx)) {
          continue;
        }
        if (curr_rule.has_below && (routing_layer_idx != below_routing_layer_idx)) {
          continue;
        }
        auto layer_it = layer_data.find(routing_layer_idx);
        if (layer_it == layer_data.end()) {
          continue;
        }
        const RVLayerData& rv_layer_data = layer_it->second;
        std::vector<std::pair<GTLRectInt, int32_t>> rect_polygon_pair_list;
        rv_layer_data.queryMaxRects(cut_gtl_rect, std::back_inserter(rect_polygon_pair_list));
        std::map<int32_t, std::vector<PlanarRect>> polygon_containing_rect_map;
        for (const auto& [gtl_rect, max_rect_id] : rect_polygon_pair_list) {
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          if (!DRCUTIL.isOpenOverlap(routing_rect, cut_rect)) {
            continue;
          }
          int32_t polygon_id = rv_layer_data.getMaxRect(max_rect_id).polygon_id;
          polygon_containing_rect_map[polygon_id].push_back(routing_rect);
        }
        if (polygon_containing_rect_map.empty()) {
          continue;
        }

        std::set<int32_t> processed_boundary_set;
        for (const auto& [polygon_id, containing_rect_list] : polygon_containing_rect_map) {
          int32_t net_idx = rv_layer_data.getNetIdxByPolygonId(polygon_id);
          const PolygonData& polygon_data = rv_layer_data.getPolygon(polygon_id);
          for (const BoundaryData& boundary_data : rv_layer_data.getBoundaries(polygon_data)) {
            int32_t boundary_id = rv_layer_data.getBoundaryId(boundary_data);
            if (DRCUTIL.exist(processed_boundary_set, boundary_id)) {
              continue;
            }
            if (!is_eol_boundary(rv_layer_data, boundary_id, curr_rule.eol_width)) {
              continue;
            }

            const BoundaryData& pre_boundary = rv_layer_data.getPrevBoundary(boundary_id);
            const BoundaryData& post_boundary = rv_layer_data.getNextBoundary(boundary_id);
            if (curr_rule.has_min_length && pre_boundary.edge_length < curr_rule.min_length && post_boundary.edge_length < curr_rule.min_length) {
              continue;
            }

            Segment<PlanarCoord> boundary_segment(boundary_data.begin_coord, boundary_data.end_coord);
            PlanarRect boundary_rect = DRCUTIL.convertToPlanarRect(boundary_data.edge);
            PlanarRect cut_edge_rect = DRCUTIL.getRect(cut_rect.getOrientEdge(boundary_data.orient));
            const PlanarRect* routing_rect_ptr = nullptr;
            for (const PlanarRect& routing_rect : containing_rect_list) {

              if (!DRCUTIL.isInside(boundary_segment, routing_rect.getOrientEdge(boundary_data.orient))) {
                continue;
              }
              if (DRCUTIL.getEuclideanDistance(boundary_rect, cut_edge_rect) >= curr_rule.overhang) {
                continue;
              }
              routing_rect_ptr = &routing_rect;
              break;
            }
            if (routing_rect_ptr == nullptr) {
              continue;
            }

            processed_boundary_set.insert(boundary_id);

            int32_t pre_segment_length = pre_boundary.edge_length;
            int32_t post_segment_length = post_boundary.edge_length;
            bool has_parallel_neighbor = false;
            {
              PlanarRect pre_rect;
              PlanarRect post_rect;
              build_parallel_windows(boundary_data, curr_rule, pre_rect, post_rect);

              PlanarRect check_rect;
              bool should_query = false;
              {
                if (pre_segment_length >= curr_rule.min_length && post_segment_length >= curr_rule.min_length) {
                  check_rect = DRCUTIL.getBoundingBox({pre_rect.get_ll(), pre_rect.get_ur(), post_rect.get_ll(), post_rect.get_ur()});
                  should_query = true;
                } else if (pre_segment_length >= curr_rule.min_length) {
                  check_rect = pre_rect;
                  should_query = true;
                } else if (post_segment_length >= curr_rule.min_length) {
                  check_rect = post_rect;
                  should_query = true;
                }
              }
              if (!should_query) {
                continue;
              }

              std::vector<std::pair<GTLRectInt, int32_t>> env_rect_polygon_pair_list;
              rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(env_rect_polygon_pair_list));

              bool has_left_neighbor = false;
              bool has_right_neighbor = false;
              for (const auto& [env_gtl_rect, env_max_rect_id] : env_rect_polygon_pair_list) {
                int32_t env_polygon_id = rv_layer_data.getMaxRect(env_max_rect_id).polygon_id;
                int32_t env_net_idx = rv_layer_data.getNetIdxByPolygonId(env_polygon_id);
                PlanarRect env_routing_rect = DRCUTIL.convertToPlanarRect(env_gtl_rect);
                if (env_net_idx == -1) {
                  continue;
                }
                if (DRCUTIL.isClosedOverlap(*routing_rect_ptr, env_routing_rect) && net_idx == env_net_idx) {
                  continue;
                }
                if (DRCUTIL.isClosedOverlap(env_routing_rect, pre_rect)) {
                  has_left_neighbor = true;
                }
                if (DRCUTIL.isClosedOverlap(env_routing_rect, post_rect)) {
                  has_right_neighbor = true;
                }
                if (has_left_neighbor || has_right_neighbor) {
                  has_parallel_neighbor = true;
                  break;
                }
              }
            }
            if (!has_parallel_neighbor) {
              continue;
            }
            Violation violation;
            violation.set_violation_type(ViolationType::kEnclosureParallel);
            violation.set_is_routing(true);
            violation.set_violation_net_set({net_idx});
            violation.set_layer_idx(below_routing_layer_idx);
            violation.set_rect(cut_rect);
            violation.set_required_size(curr_rule.overhang);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
}

}  // namespace idrc
