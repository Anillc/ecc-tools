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
#include <cstdint>

#include "RuleValidator.hpp"
#include "Utility.hpp"

namespace idrc {

void RuleValidator::verifyCutEOLSpacing(RVCluster& rv_cluster)
{
  const auto orientations = {Orientation::kEast, Orientation::kSouth, Orientation::kWest, Orientation::kNorth};
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();
  const auto& layer_data = rv_cluster.get_layer_data();

  using LayerNetRTree = bgi::rtree<std::pair<GTLRectInt, int32_t>, bgi::quadratic<16>>;

  std::map<int32_t, LayerNetRTree> routing_net_total_rtrees;
  std::map<int32_t, LayerNetRTree> routing_net_env_rtrees;

  for (const auto& [layer_idx, rv_layer_data] : layer_data) {
    std::vector<std::pair<GTLRectInt, int32_t>> total_rtree_inputs;
    std::vector<std::pair<GTLRectInt, int32_t>> env_rtree_inputs;

    total_rtree_inputs.reserve(rv_layer_data.max_rect_pool.size());
    env_rtree_inputs.reserve(rv_layer_data.max_rect_pool.size());
    for (int32_t max_rect_id = 0; max_rect_id < static_cast<int32_t>(rv_layer_data.max_rect_pool.size()); max_rect_id++) {
      const MaxRectData& max_rect_data = rv_layer_data.max_rect_pool[max_rect_id];
      int32_t net_idx = rv_layer_data.getNetIdxByMaxRectId(max_rect_id);
      total_rtree_inputs.emplace_back(max_rect_data.rect, net_idx);
      if (max_rect_data.isEnv) {
        env_rtree_inputs.emplace_back(max_rect_data.rect, net_idx);
      }
    }

    routing_net_total_rtrees[layer_idx] = LayerNetRTree(total_rtree_inputs);
    routing_net_env_rtrees[layer_idx] = LayerNetRTree(env_rtree_inputs);
  }

  // build global eol map, (layer_idx, eol_edge, length)
  std::map<int32_t, std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>>> layer_net_eol_edge_map;
  for (const auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    auto total_rtree_it = routing_net_total_rtrees.find(routing_layer_idx);
    if (total_rtree_it == routing_net_total_rtrees.end()) {
      continue;
    }
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      // if (net_idx == -1) {
      //   continue;
      // }
      for (const PolygonData& polygon_data : rv_layer_data.getPolygons(routing_net)) {
        for (const BoundaryData& boundary_data : rv_layer_data.getBoundaries(polygon_data)) {
          int32_t boundary_id = rv_layer_data.getBoundaryId(boundary_data);
          if (!(rv_layer_data.getPrevBoundary(boundary_id).isConvex && boundary_data.isConvex)) {
            continue;
          }

          PlanarRect edge_rect = DRCUTIL.convertToPlanarRect(boundary_data.edge);
          std::vector<std::pair<GTLRectInt, int32_t>> rect_net_lists;
          total_rtree_it->second.query(bgi::intersects(boundary_data.edge), std::back_inserter(rect_net_lists));
          bool is_overlapped = false;
          for (const auto& [gtl_rect, other_net_idx] : rect_net_lists) {
            PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
            if (other_net_idx != net_idx && DRCUTIL.isInside(rect, edge_rect) && DRCUTIL.isOpenOverlap(rect, edge_rect)) {
              is_overlapped = true;
              break;
            }
          }
          if (is_overlapped) {
            continue;
          }
          layer_net_eol_edge_map[routing_layer_idx][net_idx][edge_rect] = boundary_data.edge_length;
        }
      }
    }
  }

  for (const auto& [cut_layer_idx, cut_layer_data] : layer_data) {
    for (const CutData& cut_data : cut_layer_data.getCuts()) {
      // if (cut_data.isEnv && cut_data.net_idx == -1) {
      //   continue;
      // }
      int32_t cut_net_idx = cut_data.net_idx;
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_data.rect);
      // for each via, get overlaped metal
      int32_t routing_layer_idx = -1;
      {
        std::vector<int32_t> routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
        routing_layer_idx = *std::max_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
      }
      CutLayer& cut_layer = cut_layer_list[cut_layer_idx];
      int32_t eol_spacing = cut_layer.get_cut_eol_spacing_rule().eol_spacing;
      int32_t eol_prl = cut_layer.get_cut_eol_spacing_rule().eol_prl;
      int32_t eol_prl_spacing = cut_layer.get_cut_eol_spacing_rule().eol_prl_spacing;
      int32_t eol_width = cut_layer.get_cut_eol_spacing_rule().eol_width;
      int32_t smaller_overhang = cut_layer.get_cut_eol_spacing_rule().smaller_overhang;
      int32_t equal_overhang = cut_layer.get_cut_eol_spacing_rule().equal_overhang;
      int32_t side_ext = cut_layer.get_cut_eol_spacing_rule().side_ext;
      int32_t backward_ext = cut_layer.get_cut_eol_spacing_rule().backward_ext;
      int32_t span_length = cut_layer.get_cut_eol_spacing_rule().span_length;

      auto routing_layer_data_it = layer_data.find(routing_layer_idx);
      if (routing_layer_data_it == layer_data.end()) {
        continue;
      }
      const RVLayerData& routing_layer_data = routing_layer_data_it->second;

      auto total_rtree_it = routing_net_total_rtrees.find(routing_layer_idx);
      if (total_rtree_it == routing_net_total_rtrees.end()) {
        continue;
      }
      std::vector<std::pair<GTLRectInt, int32_t>> overlaped_rect;
      total_rtree_it->second.query(bgi::intersects(cut_data.rect), std::back_inserter(overlaped_rect));

      std::map<Orientation, int32_t> orient_overhang_map
          = {{Orientation::kEast, 0}, {Orientation::kSouth, 0}, {Orientation::kWest, 0}, {Orientation::kNorth, 0}};

      std::map<Orientation, int32_t> direction_span_map
          = {{Orientation::kEast, 0}, {Orientation::kSouth, 0}, {Orientation::kWest, 0}, {Orientation::kNorth, 0}};
      for (auto& [gtl_rect, net_idx] : overlaped_rect) {
        PlanarRect span_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
        if (!DRCUTIL.isOpenOverlap(span_rect, cut_rect)) {
          continue;
        }

        for (auto orient : orientations) {
          if (DRCUTIL.isInside(span_rect, DRCUTIL.getRect(cut_rect.getOrientEdge(orient)))) {
            auto& current_max = orient_overhang_map[orient];
            current_max = std::max(current_max, DRCUTIL.getOrientEdgeDistance(cut_rect, span_rect, orient));
          }
          if (orient == Orientation::kNorth) {
            direction_span_map[orient] = std::max(direction_span_map[orient], span_rect.get_ur_y() - cut_rect.get_ll_y());
          } else if (orient == Orientation::kSouth) {
            direction_span_map[orient] = std::max(direction_span_map[orient], cut_rect.get_ur_y() - span_rect.get_ll_y());
          } else if (orient == Orientation::kWest) {
            direction_span_map[orient] = std::max(direction_span_map[orient], cut_rect.get_ur_x() - span_rect.get_ll_x());
          } else if (orient == Orientation::kEast) {
            direction_span_map[orient] = std::max(direction_span_map[orient], span_rect.get_ur_x() - cut_rect.get_ll_x());
          }
        }
      }

      std::vector<std::pair<Orientation, Orientation>> check_ortho_orients;
      for (auto check_orient : orientations) {
        if (orient_overhang_map[check_orient] >= smaller_overhang) {
          continue;
        }
        auto ortho_orients = DRCUTIL.getOrthogonalOrientationList(check_orient);
        for (auto ortho_orient : ortho_orients) {
          if (orient_overhang_map[ortho_orient] == equal_overhang) {
            check_ortho_orients.push_back({check_orient, ortho_orient});
          }
        }
      }

      if (check_ortho_orients.empty()) {
        continue;
      }

      for (auto& [check_orient, ortho_orient] : check_ortho_orients) {
        for (auto [gtl_rect, net_idx] : overlaped_rect) {
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          PlanarRect check_edge = DRCUTIL.getRect(routing_rect.getOrientEdge(check_orient));
          if (!DRCUTIL.isInside(routing_rect, cut_rect)) {
            continue;
          }

          std::map<Orientation, int32_t> routing_rect_overhangs;
          for (auto orient : orientations) {
            routing_rect_overhangs[orient] = DRCUTIL.getOrientEdgeDistance(routing_rect, cut_rect, orient);
          }
          if (routing_rect_overhangs[check_orient] >= smaller_overhang || routing_rect_overhangs[ortho_orient] != equal_overhang) {
            continue;
          }

          bool has_check_span = direction_span_map[check_orient] >= span_length;
          bool has_oppo_span = direction_span_map[DRCUTIL.getOppositeOrientation(check_orient)] >= span_length;
          bool has_wide_span = has_check_span || has_oppo_span;
          bool is_eol = false;
          if (DRCUTIL.exist(layer_net_eol_edge_map[routing_layer_idx][net_idx], check_edge)) {
            if (layer_net_eol_edge_map[routing_layer_idx][net_idx][check_edge] < eol_width) {
              is_eol = true;
            }
          }

          if (!has_wide_span && !is_eol) {
            continue;
          }

          PlanarRect back_side, other_back_side;

          // ll 表示west或者south， ur表示east north
          PlanarRect ur_rect, ll_rect;
          if (check_orient == Orientation::kEast) {
            ur_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ur_y(), routing_rect.get_ur_x(), routing_rect.get_ur_y() + side_ext};
            ll_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ll_y() - side_ext, routing_rect.get_ur_x(), routing_rect.get_ll_y()};
          } else if (check_orient == Orientation::kSouth) {
            ur_rect = PlanarRect{routing_rect.get_ur_x(), routing_rect.get_ll_y(), routing_rect.get_ur_x() + side_ext, routing_rect.get_ll_y() + backward_ext};
            ll_rect = PlanarRect{routing_rect.get_ll_x() - side_ext, routing_rect.get_ll_y(), routing_rect.get_ll_x(), routing_rect.get_ll_y() + backward_ext};
          } else if (check_orient == Orientation::kWest) {
            ll_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ll_y() - side_ext, routing_rect.get_ll_x() + backward_ext, routing_rect.get_ll_y()};
            ur_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ur_y(), routing_rect.get_ll_x() + backward_ext, routing_rect.get_ur_y() + side_ext};
          } else if (check_orient == Orientation::kNorth) {
            ll_rect = {routing_rect.get_ll_x() - side_ext, routing_rect.get_ur_y() - backward_ext, routing_rect.get_ll_x(), routing_rect.get_ur_y()};
            ur_rect = {routing_rect.get_ur_x(), routing_rect.get_ur_y() - backward_ext, routing_rect.get_ur_x() + side_ext, routing_rect.get_ur_y()};
          }
          if (ortho_orient == Orientation::kEast || ortho_orient == Orientation::kNorth) {
            back_side = ur_rect;
            other_back_side = ll_rect;
          } else {
            back_side = ll_rect;
            other_back_side = ur_rect;
          }

          std::vector<std::pair<GTLRectInt, int32_t>> window_overlap_boundary_list;
          routing_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(back_side), std::back_inserter(window_overlap_boundary_list));

          bool has_check_overlap = false;
          bool is_check_edge_env = false;
          for (auto& [gtl_boundary_edge, boundary_id] : window_overlap_boundary_list) {
            const auto& boundary_data = routing_layer_data.getBoundary(boundary_id);
            PlanarRect env_rect = DRCUTIL.convertToPlanarRect(boundary_data.edge);
            if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
              continue;
            }
            if (boundary_data.orient != DRCUTIL.getOppositeOrientation(ortho_orient)) {
              continue;
            }
            if (DRCUTIL.isOpenOverlap(env_rect, back_side)) {
              has_check_overlap = true;
              is_check_edge_env = boundary_data.isEnv;
            }
          }

          if (!has_check_overlap) {
            continue;
          }

          std::vector<std::pair<GTLRectInt, int32_t>> other_window_overlap_boundary_list;
          routing_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(other_back_side), std::back_inserter(other_window_overlap_boundary_list));
          bool has_other_overlap = false;
          for (auto& [gtl_env_rect, env_boundary_id] : other_window_overlap_boundary_list) {
            const auto& boundary_data = routing_layer_data.getBoundary(env_boundary_id);
            PlanarRect env_rect = DRCUTIL.convertToPlanarRect(boundary_data.edge);
            if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
              continue;
            }
            if (boundary_data.orient != ortho_orient) {
              continue;
            }
            if (DRCUTIL.isOpenOverlap(env_rect, other_back_side)) {
              has_other_overlap = true;
            }
          }

          if (!has_oppo_span && has_other_overlap) {
            continue;
          }

          std::vector<CutData> bg_cut_rect_net_pair_list;
          cut_layer_data.queryCuts(DRCUTIL.convertToGTLRectInt(DRCUTIL.getEnlargedRect(cut_rect, eol_prl_spacing)),
                                   std::back_inserter(bg_cut_rect_net_pair_list));
          for (auto checking_orient : {DRCUTIL.getOppositeOrientation(check_orient), DRCUTIL.getOppositeOrientation(ortho_orient)}) {
            for (const auto& env_cut_data : bg_cut_rect_net_pair_list) {
              int32_t env_net_idx = env_cut_data.net_idx;
              PlanarRect env_cut_rect = DRCUTIL.convertToPlanarRect(env_cut_data.rect);
              if (cut_rect == env_cut_rect) {
                continue;
              }
              PlanarRect rect = DRCUTIL.getRect(cut_rect.getOrientEdge(checking_orient));
              PlanarRect orient_rect = rect;
              if (checking_orient == Orientation::kNorth || checking_orient == Orientation::kSouth) {
                rect = DRCUTIL.getEnlargedRect(rect, std::abs(eol_prl), 0);
                rect = DRCUTIL.getEnlargedPartRect(rect, checking_orient, eol_prl_spacing);
              } else {
                rect = DRCUTIL.getEnlargedRect(rect, 0, std::abs(eol_prl));
                rect = DRCUTIL.getEnlargedPartRect(rect, checking_orient, eol_prl_spacing);
              }
              if (checking_orient == Orientation::kEast) {
                if (orient_rect.get_ll_x() > env_cut_rect.get_ur_x()) {
                  continue;
                }
              } else if (checking_orient == Orientation::kSouth) {
                if (orient_rect.get_ur_y() < env_cut_rect.get_ll_y()) {
                  continue;
                }
              } else if (checking_orient == Orientation::kWest) {
                if (orient_rect.get_ur_x() < env_cut_rect.get_ll_x()) {
                  continue;
                }
              } else {
                if (orient_rect.get_ll_y() > env_cut_rect.get_ur_y()) {
                  continue;
                }
              }
              bool use_project_distance = DRCUTIL.isOpenOverlap(rect, env_cut_rect);
              int32_t required_size = use_project_distance ? eol_prl_spacing : eol_spacing;

              if (use_project_distance) {
                if (DRCUTIL.getProjectionDistance(cut_rect, env_cut_rect) >= required_size) {
                  continue;
                }
              } else {
                if (DRCUTIL.getEuclideanDistance(orient_rect, env_cut_rect) >= required_size) {
                  continue;
                }
              }

              // VIAx的违例输出Mx
              int32_t violation_routing_layer_idx = -1;
              {
                std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
                violation_routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
              }
              PlanarRect violation_rect;
              if (DRCUTIL.isClosedOverlap(cut_rect, env_cut_rect)) {
                violation_rect = DRCUTIL.getOverlap(cut_rect, env_cut_rect);
              } else {
                violation_rect = DRCUTIL.getSpacingRect(cut_rect, env_cut_rect);
              }

              if (cut_data.isEnv && env_cut_data.isEnv && is_check_edge_env) {
                continue;
              }

              Violation violation;
              violation.set_violation_type(ViolationType::kCutEOLSpacing);
              violation.set_is_routing(true);
              violation.set_violation_net_set({cut_net_idx, env_net_idx});
              violation.set_layer_idx(violation_routing_layer_idx);
              violation.set_rect(violation_rect);
              violation.set_required_size(required_size);
              rv_cluster.get_violation_list().push_back(violation);
            }
          }
        }
      }
    }
  }
}

}  // namespace idrc
