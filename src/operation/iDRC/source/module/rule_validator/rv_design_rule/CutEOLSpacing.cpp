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

void RuleValidator::verifyCutEOLSpacing(RVCluster& rv_cluster)
{
  const auto orientations = {Orientation::kEast, Orientation::kSouth, Orientation::kWest, Orientation::kNorth};
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();

  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_total_polysets;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_net_total_rtrees, routing_net_env_rtrees;

  // preprocess, build rtrees
  {
    std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_env_polysets;

    auto add_to_polyset = [&](DRCShape* shape, bool to_env) {
      if (!shape->get_is_routing()) {
        return;
      }
      auto rect = DRCUTIL.convertToGTLRectInt(shape->get_rect());
      int32_t l_idx = shape->get_layer_idx(), n_idx = shape->get_net_idx();
      routing_net_total_polysets[l_idx][n_idx].insert(rect);
      if (to_env) {
        routing_net_env_polysets[l_idx][n_idx].insert(rect);
      }
    };

    for (auto* s : rv_cluster.get_drc_env_shape_list()) {
      add_to_polyset(s, true);
    }
    for (auto* s : rv_cluster.get_drc_result_shape_list()) {
      add_to_polyset(s, false);
    }

    auto build_rtrees = [&](const auto& poly_set_map, auto& rtree_map) {
      for (const auto& [layer_idx, net_map] : poly_set_map) {
        std::vector<std::pair<BGRectInt, int32_t>> rtree_inputs;
        for (const auto& [net_idx, poly_set] : net_map) {
          std::vector<GTLRectInt> rects;
          gtl::get_max_rectangles(rects, poly_set);
          for (auto& r : rects) {
            rtree_inputs.emplace_back(DRCUTIL.convertToBGRectInt(r), net_idx);
          }
        }
        rtree_map[layer_idx] = {rtree_inputs.begin(), rtree_inputs.end()};
      }
    };

    build_rtrees(routing_net_total_polysets, routing_net_total_rtrees);
    build_rtrees(routing_net_env_polysets, routing_net_env_rtrees);
  }

  // build cut shape R-tree
  std::map<int32_t, std::map<int32_t, std::vector<PlanarRect>>> layer_net_cut_shape_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> cut_bg_rtree_map;
  {
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (!drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        cut_bg_rtree_map[drc_shape->get_layer_idx()].insert(std::make_pair(DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()));
        layer_net_cut_shape_map[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(drc_shape->get_rect());
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        cut_bg_rtree_map[drc_shape->get_layer_idx()].insert(std::make_pair(DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()));
        layer_net_cut_shape_map[drc_shape->get_layer_idx()][drc_shape->get_net_idx()].push_back(drc_shape->get_rect());
      }
    }
  }

  // build global eol map, (layer_idx, eol_edge, length)
  std::map<int32_t, std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>>> layer_net_eol_edge_map;
  for (auto& [routing_layer_idx, net_gtl_poly_set] : routing_net_total_polysets) {
    for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set) {
      std::vector<GTLHolePolyInt> hole_poly_list;
      gtl_poly_set.get(hole_poly_list);
      for (GTLHolePolyInt hole_poly : hole_poly_list) {
        // consider hole
        std::vector<std::pair<GTLHolePolyInt, bool>> check_hole_pair_list;
        {
          check_hole_pair_list.emplace_back(hole_poly, false);
          for (auto iter = hole_poly.begin_holes(); iter != hole_poly.end_holes(); iter++) {
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
          for (auto iter = check_hole_poly.begin(); iter != check_hole_poly.end(); iter++) {
            coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
          }
          std::vector<bool> convex_corner_list;
          std::vector<Segment<PlanarCoord>> edge_list;
          for (int32_t i = 0; i < coord_size; i++) {
            PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
            PlanarCoord& curr_coord = coord_list[i];
            PlanarCoord& post_coord = coord_list[getIdx(i + 1, coord_size)];
            if (is_hole) {
              convex_corner_list.push_back(DRCUTIL.isConcaveCorner(DRCUTIL.getRotation(check_hole_poly), pre_coord, curr_coord, post_coord));
            } else {
              convex_corner_list.push_back(DRCUTIL.isConvexCorner(DRCUTIL.getRotation(check_hole_poly), pre_coord, curr_coord, post_coord));
            }
            edge_list.push_back(Segment<PlanarCoord>(pre_coord, curr_coord));
          }
          for (int32_t i = 0; i < coord_size; i++) {
            if (convex_corner_list[getIdx(i - 1, coord_size)] && convex_corner_list[i]) {
              // 只要是两个凸角，且不被其他net包含即认为是eol
              PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
              PlanarCoord& curr_coord = coord_list[i];
              auto edge_rect = DRCUTIL.getRect(pre_coord, curr_coord);
              std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_lists;
              routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(edge_rect)), std::back_inserter(bg_rect_net_lists));
              bool is_overlapped = false;
              for (auto [bg_rect, netidx] : bg_rect_net_lists) {
                PlanarRect rect = DRCUTIL.convertToPlanarRect(bg_rect);
                if (netidx != net_idx && DRCUTIL.isInside(rect, edge_rect) && DRCUTIL.isOpenOverlap(rect, edge_rect)) {
                  is_overlapped = true;
                  break;
                }
              }
              if (is_overlapped) {
                continue;
              }
              int32_t curr_length = DRCUTIL.getManhattanDistance(pre_coord, curr_coord);
              layer_net_eol_edge_map[routing_layer_idx][net_idx][edge_rect] = curr_length;
            }
          }
        }
      }
    }
  }

  for (auto [cut_layer_idx, net_idx_cut_rect] : layer_net_cut_shape_map) {
    for (auto [cut_net_idx, cut_rect_list] : net_idx_cut_rect) {
      for (auto cut_rect : cut_rect_list) {
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

        std::vector<std::pair<BGRectInt, int32_t>> overlaped_rect;
        routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(cut_rect)), std::back_inserter(overlaped_rect));

        std::map<Orientation, int32_t> orient_overhang_map
            = {{Orientation::kEast, 0}, {Orientation::kSouth, 0}, {Orientation::kWest, 0}, {Orientation::kNorth, 0}};
        for (auto& [bg_rect, net_idx] : overlaped_rect) {
          PlanarRect span_rect = DRCUTIL.convertToPlanarRect(bg_rect);
          if (!DRCUTIL.isOpenOverlap(span_rect, cut_rect))
            continue;

          for (auto orient : orientations) {
            if (DRCUTIL.isInside(span_rect, DRCUTIL.getRect(cut_rect.getOrientEdge(orient)))) {
              auto& current_max = orient_overhang_map[orient];
              current_max = std::max(current_max, DRCUTIL.getOrientEdgeDistance(cut_rect, span_rect, orient));
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
          Direction direction = (check_orient == Orientation::kNorth || check_orient == Orientation::kSouth) ? Direction::kVertical : Direction::kHorizontal;
          for (auto [bg_rect, net_idx] : overlaped_rect) {
            PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(bg_rect);
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

            // 与routing rect相交的矩形，计算span length
            int32_t ll_span = 0, ur_span = 0;
            std::vector<std::pair<BGRectInt, int32_t>> rect_overlap_bg_list;
            routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(routing_rect)),
                                                              std::back_inserter(rect_overlap_bg_list));
            GTLPolySetInt all_neighboors;
            for (auto& [bg_overlap_env, overlap_env_net] : rect_overlap_bg_list) {
              all_neighboors += DRCUTIL.convertToGTLRectInt(bg_overlap_env);
              PlanarRect overlap_env = DRCUTIL.convertToPlanarRect(bg_overlap_env);
              if (!DRCUTIL.isOpenOverlap(overlap_env, cut_rect)) {
                continue;
              }
              if (DRCUTIL.isInside(DRCUTIL.getRect(overlap_env.getOrientEdge(check_orient)), check_edge)) {
                check_edge = DRCUTIL.getRect(overlap_env.getOrientEdge(check_orient));
              }
              if (direction == Direction::kVertical) {
                ll_span = std::max(ll_span, cut_rect.get_ll_x() - overlap_env.get_ll_x());
                ur_span = std::max(ur_span, overlap_env.get_ur_x() - cut_rect.get_ur_x());
              } else if (direction == Direction::kHorizontal) {
                ll_span = std::max(ll_span, cut_rect.get_ll_y() - overlap_env.get_ll_y());
                ur_span = std::max(ur_span, overlap_env.get_ur_y() - cut_rect.get_ur_y());
              }
            }

            // has_wide_span 或者 eol， 进一步检查
            int32_t back_side_span, other_back_side_span;
            if (ortho_orient == Orientation::kWest || ortho_orient == Orientation::kSouth) {
              back_side_span = ll_span;
              other_back_side_span = ur_span;
            } else {
              back_side_span = ur_span;
              other_back_side_span = ll_span;
            }
            // if (back_side_span + routing_rect.getWidth() >= span_length) { continue; }
            bool has_ll_span = (ll_span + routing_rect.getWidth()) >= span_length;
            bool has_ur_span = (ur_span + routing_rect.getWidth() >= span_length);
            bool has_wide_span = has_ll_span || has_ur_span;
            bool is_eol = false;
            if (DRCUTIL.exist(layer_net_eol_edge_map[routing_layer_idx][net_idx], check_edge)) {
              if (layer_net_eol_edge_map[routing_layer_idx][net_idx][check_edge] < eol_width) {
                is_eol = true;
              }
            }
            if (!has_wide_span && !is_eol) {
              continue;
            }

            int32_t ll_ext = has_ll_span ? span_length : side_ext;
            int32_t ur_ext = has_ur_span ? span_length : side_ext;

            PlanarRect back_side, other_back_side;

            // ll 表示west或者south， ur表示east north
            PlanarRect ur_rect, ll_rect;
            if (check_orient == Orientation::kEast) {
              ur_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ur_y(), routing_rect.get_ur_x(), routing_rect.get_ur_y() + ur_ext};
              ll_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ll_y() - ll_ext, routing_rect.get_ur_x(), routing_rect.get_ll_y()};
            } else if (check_orient == Orientation::kSouth) {
              ur_rect = PlanarRect{routing_rect.get_ur_x(), routing_rect.get_ll_y(), routing_rect.get_ur_x() + ur_ext, routing_rect.get_ll_y() + backward_ext};
              ll_rect = PlanarRect{routing_rect.get_ll_x() - ll_ext, routing_rect.get_ll_y(), routing_rect.get_ll_x(), routing_rect.get_ll_y() + backward_ext};
            } else if (check_orient == Orientation::kWest) {
              ll_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ll_y() - ll_ext, routing_rect.get_ll_x() + backward_ext, routing_rect.get_ll_y()};
              ur_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ur_y(), routing_rect.get_ll_x() + backward_ext, routing_rect.get_ur_y() + ur_ext};
            } else if (check_orient == Orientation::kNorth) {
              ll_rect = {routing_rect.get_ll_x() - ll_ext, routing_rect.get_ur_y() - backward_ext, routing_rect.get_ll_x(), routing_rect.get_ur_y()};
              ur_rect = {routing_rect.get_ur_x(), routing_rect.get_ur_y() - backward_ext, routing_rect.get_ur_x() + ur_ext, routing_rect.get_ur_y()};
            }
            if (ortho_orient == Orientation::kEast || ortho_orient == Orientation::kNorth) {
              back_side = ur_rect;
              other_back_side = ll_rect;
            } else {
              back_side = ll_rect;
              other_back_side = ur_rect;
            }

            std::vector<std::pair<BGRectInt, int32_t>> window_overlap_rect_list;
            routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(back_side)),
                                                              std::back_inserter(window_overlap_rect_list));

            GTLPolySetInt neighbor_shape;
            neighbor_shape += DRCUTIL.convertToGTLRectInt(back_side);
            neighbor_shape -= all_neighboors;

            for (auto [bg_rect, env_net_idx] : window_overlap_rect_list) {
              if (DRCUTIL.isClosedOverlap(DRCUTIL.convertToPlanarRect(bg_rect), routing_rect)) {
                neighbor_shape -= DRCUTIL.convertToGTLRectInt(bg_rect);
              }
            }
            std::vector<GTLPolyInt> non_overlap_polygons;
            neighbor_shape.get(non_overlap_polygons);
            PlanarRect empty_region;
            for (auto& polygon : non_overlap_polygons) {
              GTLRectInt empty_gtl_rect;
              gtl::extents(empty_gtl_rect, polygon);
              if (DRCUTIL.isClosedOverlap(check_edge, DRCUTIL.convertToPlanarRect(empty_gtl_rect))) {
                empty_region = DRCUTIL.convertToPlanarRect(empty_gtl_rect);
                break;
              }
            }
            empty_region = DRCUTIL.getOverlap(empty_region, back_side);

            bool has_check_overlap = false;
            PlanarRect check_overlap_rect;
            for (auto& [bg_env_rect, bg_rect_net] : window_overlap_rect_list) {
              PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
              if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
                continue;
              }
              if (DRCUTIL.isOpenOverlap(empty_region, env_rect)) {
                has_check_overlap = true;
                check_overlap_rect = env_rect;
                break;
              }
            }

            if (!has_check_overlap) {
              continue;
            }

            std::vector<std::pair<BGRectInt, int32_t>> other_window_overlap_rect_list;
            routing_net_total_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(other_back_side)),
                                                              std::back_inserter(other_window_overlap_rect_list));
            bool has_other_overlap = false;
            PlanarRect other_enlarge_rect = DRCUTIL.getEnlargedRect(routing_rect, DRCUTIL.getOppositeOrientation(ortho_orient), other_back_side_span);
            for (auto& [bg_env_rect, bg_env_net] : other_window_overlap_rect_list) {
              PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
              if (DRCUTIL.isClosedOverlap(env_rect, other_enlarge_rect)) {
                continue;
              }
              if (DRCUTIL.isOpenOverlap(other_back_side, env_rect)) {
                has_other_overlap = true;
                break;
              }
            }

            if (has_other_overlap) {
              continue;
            }

            std::vector<std::pair<BGRectInt, int32_t>> bg_cut_rect_net_pair_list;
            cut_bg_rtree_map[cut_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(DRCUTIL.getEnlargedRect(cut_rect, eol_prl_spacing))),
                                                  std::back_inserter(bg_cut_rect_net_pair_list));
            for (auto checking_orient : {DRCUTIL.getOppositeOrientation(check_orient), DRCUTIL.getOppositeOrientation(ortho_orient)}) {
              for (auto& [bg_env_cut_rect, env_net_idx] : bg_cut_rect_net_pair_list) {
                PlanarRect env_cut_rect = DRCUTIL.convertToPlanarRect(bg_env_cut_rect);
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
                  if (orient_rect.get_ll_x() >= env_cut_rect.get_ur_x())
                    continue;
                } else if (checking_orient == Orientation::kSouth) {
                  if (orient_rect.get_ll_y() <= env_cut_rect.get_ll_y())
                    continue;
                } else if (checking_orient == Orientation::kWest) {
                  if (orient_rect.get_ll_x() <= env_cut_rect.get_ll_x())
                    continue;
                } else {
                  if (orient_rect.get_ll_y() >= env_cut_rect.get_ur_y())
                    continue;
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

                // 排除env的影响
                // 如果只使用env可以有相同的span length 和相同的 env rect
                {
                  std::vector<std::pair<BGRectInt, int32_t>> window_overlap_rect_list;
                  routing_net_env_rtrees[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(violation_rect)),
                                                                  std::back_inserter(window_overlap_rect_list));
                  bool has_check_edge = false;
                  PlanarRect same_rect;
                  for (auto& [bg_env_rect, bg_env_net] : window_overlap_rect_list) {
                    PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
                    if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
                      for (auto env_orient : orientations) {
                        if (check_edge == DRCUTIL.getRect(env_rect.getOrientEdge(env_orient))) {
                          has_check_edge = true;
                          same_rect = env_rect;
                          break;
                        }
                      }
                    }
                  }
                  if (has_check_edge) {
                    bool has_env_overlap = false;
                    for (auto& [bg_env_rect, bg_env_net] : window_overlap_rect_list) {
                      PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
                      if (DRCUTIL.isOpenOverlap(check_overlap_rect, env_rect) && DRCUTIL.isClosedOverlap(violation_rect, env_rect)) {
                        has_env_overlap = true;
                        break;
                      }
                    }
                    if (has_env_overlap) {
                      continue;
                    }
                  }
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
}

}  // namespace idrc