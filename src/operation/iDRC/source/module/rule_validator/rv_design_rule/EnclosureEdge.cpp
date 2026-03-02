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

struct ConvexInfo
{
  int32_t other_length;
  Segment<PlanarCoord> curr_edge;
  Segment<PlanarCoord> adj_edge;
  Orientation curr_orient;
  Orientation adj_orient;
};

void RuleValidator::verifyEnclosureEdge(RVCluster& rv_cluster)
{
  const auto orientations = {Orientation::kEast, Orientation::kSouth, Orientation::kWest, Orientation::kNorth};
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();
  std::map<int32_t, std::map<int32_t, std::map<PlanarRect, std::vector<ConvexInfo>, CmpPlanarRectByXASC>>> layer_net_convex_info_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_bg_rtree_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_env_rtree_map;
  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_gtl_poly_set_map;
  std::map<int32_t, std::map<int32_t, GTLPolySetInt>> routing_net_env_poly_set_map;
  //============================================================
  // preprocess
  {
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing()) {
        PlanarRect rect = drc_shape->get_rect();
        if (rect.get_ll_x() >= rect.get_ur_x() || rect.get_ll_y() >= rect.get_ur_y()) {
          continue;
        }
        routing_net_gtl_poly_set_map[drc_shape->get_layer_idx()][drc_shape->get_net_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
        routing_net_env_poly_set_map[drc_shape->get_layer_idx()][drc_shape->get_net_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing()) {
        routing_net_gtl_poly_set_map[drc_shape->get_layer_idx()][drc_shape->get_net_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
      }
    }
    for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_env_poly_set_map) {
      for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
        std::vector<GTLRectInt> gtl_rect_list;
        gtl::get_max_rectangles(gtl_rect_list, gtl_poly_set);
        for (GTLRectInt& gtl_rect : gtl_rect_list) {
          routing_env_rtree_map[routing_layer_idx].insert(std::make_pair(DRCUTIL.convertToBGRectInt(gtl_rect), net_idx));
        }
      }
    }
    // build routing_rect R-tree and convex info structs
    for (auto& [routing_layer_idx, net_gtl_poly_set_map] : routing_net_gtl_poly_set_map) {
      for (auto& [net_idx, gtl_poly_set] : net_gtl_poly_set_map) {
        // build global convex rect
        std::vector<GTLHolePolyInt> gtl_hole_poly_list;
        gtl_poly_set.get(gtl_hole_poly_list);
        for (GTLHolePolyInt gtl_hole_poly : gtl_hole_poly_list) {
          int32_t coord_size = static_cast<int32_t>(gtl_hole_poly.size());
          if (coord_size < 6) {
            continue;
          }
          std::vector<PlanarCoord> coord_list;
          for (auto iter = gtl_hole_poly.begin(); iter != gtl_hole_poly.end(); iter++) {
            coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
          }
          std::vector<bool> convex_corner_list;
          std::vector<Segment<PlanarCoord>> edge_list;
          for (int32_t i = 0; i < coord_size; i++) {
            PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
            PlanarCoord& curr_coord = coord_list[i];
            PlanarCoord& post_coord = coord_list[getIdx(i + 1, coord_size)];
            convex_corner_list.push_back(DRCUTIL.isConvexCorner(DRCUTIL.getRotation(gtl_hole_poly), pre_coord, curr_coord, post_coord));
            edge_list.push_back(Segment<PlanarCoord>(pre_coord, curr_coord));
          }
          for (int32_t i = 0; i < coord_size; i++) {
            PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
            PlanarCoord& curr_coord = coord_list[i];
            PlanarCoord& post_coord = coord_list[getIdx(i + 1, coord_size)];
            // PlanarCoord& ppost_coord = coord_list[getIdx(i + 2, coord_size)];

            if (convex_corner_list[getIdx(i - 1, coord_size)] && convex_corner_list[getIdx(i, coord_size)] && convex_corner_list[getIdx(i + 1, coord_size)]) {
              PlanarRect enc_rect = DRCUTIL.getRect(pre_coord, post_coord);
              ConvexInfo convex_info_i, convex_info_i1;
              convex_info_i.other_length = DRCUTIL.getManhattanDistance(curr_coord, post_coord);
              convex_info_i.curr_edge = edge_list[getIdx(i, coord_size)];
              convex_info_i.adj_edge = edge_list[getIdx(i - 1, coord_size)];
              convex_info_i.curr_orient = DRCUTIL.getTouchedEdgeOrient(enc_rect, edge_list[i]);
              convex_info_i.adj_orient = DRCUTIL.getTouchedEdgeOrient(enc_rect, edge_list[getIdx(i - 1, coord_size)]);

              convex_info_i1.other_length = DRCUTIL.getManhattanDistance(curr_coord, pre_coord);
              convex_info_i1.curr_edge = edge_list[getIdx(i + 1, coord_size)];
              convex_info_i1.adj_edge = edge_list[getIdx(i + 2, coord_size)];
              convex_info_i1.curr_orient = DRCUTIL.getTouchedEdgeOrient(enc_rect, edge_list[getIdx(i + 1, coord_size)]);
              convex_info_i1.adj_orient = DRCUTIL.getTouchedEdgeOrient(enc_rect, edge_list[getIdx(i + 2, coord_size)]);

              layer_net_convex_info_map[routing_layer_idx][net_idx][enc_rect].push_back(convex_info_i);
              layer_net_convex_info_map[routing_layer_idx][net_idx][enc_rect].push_back(convex_info_i1);
            }
          }
        }
        std::vector<GTLRectInt> gtl_rect_list;
        gtl::get_max_rectangles(gtl_rect_list, gtl_poly_set);
        for (GTLRectInt& gtl_rect : gtl_rect_list) {
          routing_bg_rtree_map[routing_layer_idx].insert(std::make_pair(DRCUTIL.convertToBGRectInt(gtl_rect), net_idx));
        }
      }
    }
  }
  std::map<int32_t, std::vector<PlanarRect>> cut_rect_map;
  std::set<int32_t> cut_layers;
  {
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        cut_rect_map[drc_shape->get_layer_idx()].push_back(drc_shape->get_rect());
        cut_layers.insert(drc_shape->get_layer_idx());
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        cut_rect_map[drc_shape->get_layer_idx()].push_back(drc_shape->get_rect());
        cut_layers.insert(drc_shape->get_layer_idx());
      }
    }
  }
  // build routing layer [with width]
  // {layer_idx, width} => R-tree of {wide rect, net}
  std::map<std::pair<int32_t, int32_t>, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> layer_width_rtrees;
  {
    std::map<int32_t, std::vector<int32_t>> routing_widths;
    for (int32_t cut_layer : cut_layers) {
      std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer];
      int32_t above_routing_layer_idx = *std::max_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
      int32_t below_routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());

      std::vector<EnclosureEdgeRule> sorted_rules = cut_layer_list[cut_layer].get_enclosure_edge_rule_list();
      std::sort(sorted_rules.begin(), sorted_rules.end(), [](const EnclosureEdgeRule& a, const EnclosureEdgeRule& b) {
        if (a.has_convexcorners != b.has_convexcorners) {
          return a.has_convexcorners;
        }
        return a.min_width > b.min_width;  // 宽度大的排在前
      });

      for (auto rule : sorted_rules) {
        if (!rule.has_below) {
          if (!rule.has_convexcorners) {
            routing_widths[above_routing_layer_idx].push_back(rule.min_width);
          }
        }
        if (!rule.has_above) {
          if (!rule.has_convexcorners) {
            routing_widths[below_routing_layer_idx].push_back(rule.min_width);
          }
        }
      }
    }

    for (auto& [layer_idx, net_gtl_set_map] : routing_net_gtl_poly_set_map) {
      const auto& width_ranges = routing_widths[layer_idx];
      int32_t min_width = 0;
      auto it = std::min_element(width_ranges.begin(), width_ranges.end());
      if (it != width_ranges.end()) {
        min_width = *it;
      }
      for (auto& [net_idx, gtl_poly_set] : net_gtl_set_map) {
        // eg:  with width [0.07, 0.055)
        int32_t last_width = std::numeric_limits<int32_t>::max();

        // filter out some narrow rect
        GTLPolySetInt wide_polyset = gtl_poly_set;

        gtl::resize(wide_polyset, -(min_width / 2));
        gtl::resize(wide_polyset, (min_width / 2));

        for (int32_t width : width_ranges) {
          auto& curr_tree = layer_width_rtrees[{layer_idx, width}];
          GTLPolySetInt remain_polyset = wide_polyset;
          if (width > min_width) {
            GTLRectInt net_bbox;
            gtl::extents(net_bbox, remain_polyset);
            if (gtl::delta(net_bbox, gtl::HORIZONTAL) < width && gtl::delta(net_bbox, gtl::VERTICAL) < width) {
              continue;
            }
          }
          int32_t resize_delta = width / 2;
          gtl::resize(remain_polyset, -resize_delta);
          gtl::resize(remain_polyset, resize_delta);

          std::vector<GTLRectInt> gtl_rects;
          gtl::get_max_rectangles(gtl_rects, remain_polyset);

          std::vector<GTLRectInt> dut_rects, checked_rects;
          for (GTLRectInt gtl_rect : gtl_rects) {
            PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
            if (rect.getWidth() >= last_width) {
              checked_rects.push_back(gtl_rect);
            } else {
              dut_rects.push_back(gtl_rect);
            }
          }
          GTLPolySetInt checked_poly;
          checked_poly.insert(checked_rects.begin(), checked_rects.end());

          for (GTLRectInt dut_rect : dut_rects) {
            GTLPolySetInt dut_polyset;
            dut_polyset = dut_polyset + dut_rect - checked_poly;
            std::vector<GTLPolyInt> dut_polys;
            dut_polyset.get(dut_polys);
            for (auto dut_poly : dut_polys) {
              GTLRectInt temp;
              if (dut_poly.size() > 4) {
                gtl::extents(temp, dut_poly);
              } else {
                std::vector<GTLRectInt> temp_rects;
                gtl::get_max_rectangles(temp_rects, dut_poly);
                if (!temp_rects.empty()) {
                  temp = temp_rects[0];
                }
              }
              curr_tree.insert({DRCUTIL.convertToBGRectInt(temp), net_idx});
            }
          }

          last_width = width;
        }
      }
    }
  }

  // =========================================================================
  // rules: check each cut

  for (auto& [cut_layer_idx, cut_rect_list] : cut_rect_map) {
    std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
    if (routing_layer_idx_list.size() < 2) {
      continue;
    }
    int32_t above_routing_layer_idx = *std::max_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    int32_t below_routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());

    std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_pair_list;
    std::vector<std::pair<BGRectInt, int32_t>> wide_rect_nets;
    for (PlanarRect& cut_rect : cut_rect_list) {
      BGRectInt cut_bg_rect = DRCUTIL.convertToBGRectInt(cut_rect);
      for (int32_t routing_layer_idx : routing_layer_idx_list) {
        bg_rect_net_pair_list.clear();

        routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(cut_bg_rect), std::back_inserter(bg_rect_net_pair_list));
        std::map<Orientation, int32_t> orient_overhang_map
            = {{Orientation::kEast, 0}, {Orientation::kSouth, 0}, {Orientation::kWest, 0}, {Orientation::kNorth, 0}};
        for (auto& [bg_rect, net_idx] : bg_rect_net_pair_list) {
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(bg_rect);
          if (!DRCUTIL.isOpenOverlap(routing_rect, cut_rect)) {
            continue;
          }
          for (auto orient : orientations) {
            if (DRCUTIL.isInside(routing_rect, DRCUTIL.getRect(cut_rect.getOrientEdge(orient)))) {
              auto& current_max = orient_overhang_map[orient];
              current_max = std::max(current_max, DRCUTIL.getOrientEdgeDistance(cut_rect, routing_rect, orient));
            }
          }
        }

        for (EnclosureEdgeRule& curr_rule : cut_layer_list[cut_layer_idx].get_enclosure_edge_rule_list()) {
          if (curr_rule.has_above && (routing_layer_idx != above_routing_layer_idx)) {
            continue;
          }
          if (curr_rule.has_below && (routing_layer_idx != below_routing_layer_idx)) {
            continue;
          }
          if (curr_rule.has_convexcorners) {
            continue;
          }
          wide_rect_nets.clear();
          layer_width_rtrees[{routing_layer_idx, curr_rule.min_width}].query(bgi::intersects(cut_bg_rect), std::back_inserter(wide_rect_nets));
          // query routing/width rtree, get [net, rect]
          for (auto& [wide_gtl_rect, net_idx] : wide_rect_nets) {
            PlanarRect wide_rect = DRCUTIL.convertToPlanarRect(wide_gtl_rect);
            GTLPolySetInt& polyset = routing_net_gtl_poly_set_map[routing_layer_idx][net_idx];
            if (!DRCUTIL.isOpenOverlap(wide_rect, cut_rect)) {
              continue;
            }
            std::map<Orientation, PlanarRect> orient_extension_rects;
            {
              orient_extension_rects[Orientation::kSouth] = DRCUTIL.getEnlargedRect(wide_rect.get_ll(), 0, curr_rule.par_within, wide_rect.getXSpan(), 0);
              orient_extension_rects[Orientation::kNorth] = DRCUTIL.getEnlargedRect(wide_rect.get_ur(), wide_rect.getXSpan(), 0, 0, curr_rule.par_within);
              orient_extension_rects[Orientation::kWest] = DRCUTIL.getEnlargedRect(wide_rect.get_ll(), curr_rule.par_within, 0, 0, wide_rect.getYSpan());
              orient_extension_rects[Orientation::kEast] = DRCUTIL.getEnlargedRect(wide_rect.get_ur(), 0, wide_rect.getYSpan(), curr_rule.par_within, 0);
            }
            for (Orientation orient : orientations) {
              int32_t orient_enclosure = std::max(DRCUTIL.getOrientEnclosure(wide_rect, cut_rect, orient), orient_overhang_map[orient]);
              if (orient_enclosure >= curr_rule.overhang)
                continue;

              std::vector<std::pair<BGRectInt, int32_t>> neighbor_bg_rects;
              {
                PlanarRect check_rect = DRCUTIL.getEnlargedRect(wide_rect, curr_rule.par_within);
                routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)), std::back_inserter(neighbor_bg_rects));
              }
              std::set<int32_t> check_side_par_set;
              std::set<int32_t> oppo_side_par_set;
              for (auto& [bg_rect, env_net_idx] : neighbor_bg_rects) {
                PlanarRect neighbor_rect = DRCUTIL.convertToPlanarRect(bg_rect);
                if (DRCUTIL.isClosedOverlap(neighbor_rect, wide_rect)) {
                  continue;
                }

                int32_t prl = DRCUTIL.getParallelLength(wide_rect, neighbor_rect);
                if (DRCUTIL.isOpenOverlap(neighbor_rect, orient_extension_rects[orient])
                    || DRCUTIL.isOpenOverlap(neighbor_rect, orient_extension_rects[DRCUTIL.getOppositeOrientation(orient)])) {
                  if (net_idx == env_net_idx && prl > curr_rule.par_length) {
                    prl = DRCUTIL.getDirectPRL(polyset, wide_rect, neighbor_rect);
                  }
                }

                if (DRCUTIL.isOpenOverlap(neighbor_rect, orient_extension_rects[orient])) {
                  if (prl > curr_rule.par_length) {
                    if (DRCUTIL.getParallelLength(neighbor_rect, cut_rect) > 0) {
                      check_side_par_set.insert(env_net_idx);
                    }
                  }
                }

                if (DRCUTIL.isOpenOverlap(neighbor_rect, orient_extension_rects[DRCUTIL.getOppositeOrientation(orient)])) {
                  if (prl > curr_rule.par_length) {
                    if (DRCUTIL.getParallelLength(neighbor_rect, cut_rect) > 0) {
                      oppo_side_par_set.insert(env_net_idx);
                    }
                  }
                }
              }
              if (curr_rule.has_except_two_edges && !check_side_par_set.empty() && !oppo_side_par_set.empty()) {
                continue;
              }
              if (!check_side_par_set.empty() && orient_enclosure < curr_rule.overhang) {
                Violation violation;
                violation.set_violation_type(ViolationType::kEnclosureEdge);
                violation.set_is_routing(true);
                violation.set_violation_net_set({net_idx, *check_side_par_set.begin()});
                violation.set_layer_idx(below_routing_layer_idx);
                violation.set_rect(cut_rect);
                violation.set_required_size(curr_rule.overhang);
                rv_cluster.get_violation_list().push_back(violation);
              }
            }
          }
        }

        //  =====================================================================
        //  convex corner rule
        for (auto& [bg_rect, net_idx] : bg_rect_net_pair_list) {
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(bg_rect);
          if (!DRCUTIL.isInside(routing_rect, cut_rect)) {
            continue;
          }
          EnclosureEdgeRule convex_rule;
          for (auto rule : cut_layer_list[cut_layer_idx].get_enclosure_edge_rule_list()) {
            if (rule.has_convexcorners) {
              convex_rule = rule;
              break;
            }
          }
          if (convex_rule.has_above && (routing_layer_idx != above_routing_layer_idx)) {
            continue;
          }
          if (convex_rule.has_below && (routing_layer_idx != below_routing_layer_idx)) {
            continue;
          }

          PlanarRect curr_par_rect;
          PlanarRect adj_par_rect;
          Orientation checking_orient;

          if (!DRCUTIL.exist(layer_net_convex_info_map[routing_layer_idx][net_idx], routing_rect)) {
            continue;
          }

          for (auto convex_info : layer_net_convex_info_map[routing_layer_idx][net_idx][routing_rect]) {
            int32_t curr_length = DRCUTIL.getManhattanDistance(convex_info.curr_edge.get_first(), convex_info.curr_edge.get_second());
            int32_t adj_length = DRCUTIL.getManhattanDistance(convex_info.adj_edge.get_first(), convex_info.adj_edge.get_second());
            checking_orient = convex_info.curr_orient;

            if (curr_length > convex_rule.convex_length || convex_info.other_length > convex_rule.adjacent_length || adj_length < convex_rule.length) {
              continue;
            }
            if (orient_overhang_map[checking_orient] >= convex_rule.overhang) {
              continue;
            }
            curr_par_rect = DRCUTIL.getEnlargedPartRect(routing_rect, convex_info.curr_orient, convex_rule.convex_par_within);
            adj_par_rect = DRCUTIL.getEnlargedPartRect(routing_rect, convex_info.adj_orient, convex_rule.convex_par_within);

            std::vector<std::pair<BGRectInt, int32_t>> env_bg_rect_net_pair_list;
            std::vector<std::pair<BGRectInt, int32_t>> env_only_rect_net_pair_list;
            {
              PlanarRect check_rect = DRCUTIL.getEnlargedRect(routing_rect, convex_rule.convex_par_within);
              routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)),
                                                            std::back_inserter(env_bg_rect_net_pair_list));
              routing_env_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)),
                                                             std::back_inserter(env_only_rect_net_pair_list));
            }
            int32_t max_env_width = 0;
            // 排除env
            std::set<int32_t> env_curr_set;
            std::set<int32_t> env_adj_set;
            for (auto [env_only_bg_rect, env_only_net_idx] : env_only_rect_net_pair_list) {
              PlanarRect env_only_rect = DRCUTIL.convertToPlanarRect(env_only_bg_rect);
              if (DRCUTIL.isClosedOverlap(routing_rect, env_only_rect)) {
                continue;
              }

              if (DRCUTIL.isOpenOverlap(env_only_rect, curr_par_rect)) {
                if (DRCUTIL.getParallelLength(env_only_rect, cut_rect) > 0) {
                  env_curr_set.insert(env_only_net_idx);
                  if (env_only_rect.getWidth() > max_env_width) {
                    max_env_width = env_only_rect.getWidth();
                  }
                }
              }

              if (DRCUTIL.isOpenOverlap(env_only_rect, adj_par_rect)) {
                if (DRCUTIL.getParallelLength(env_only_rect, cut_rect) > 0) {
                  env_adj_set.insert({env_only_net_idx});
                }
              }
            }
            if (env_curr_set.empty() || env_adj_set.empty()) {
              max_env_width = 0;
            }

            std::set<int32_t> curr_par_net_idx_set;
            std::set<int32_t> adj_par_net_idx_set;
            for (auto& [bg_env_rect, env_net_idx] : env_bg_rect_net_pair_list) {
              PlanarRect env_routing_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
              if (DRCUTIL.isClosedOverlap(cut_rect, env_routing_rect)) {
                continue;
              }
              if (DRCUTIL.isOpenOverlap(env_routing_rect, curr_par_rect)) {
                if (DRCUTIL.getParallelLength(env_routing_rect, cut_rect) > 0) {
                  if (env_routing_rect.getWidth() > max_env_width) {
                    curr_par_net_idx_set.insert({env_net_idx});
                  }
                }
              }
              if (DRCUTIL.isOpenOverlap(env_routing_rect, adj_par_rect)) {
                if (DRCUTIL.getParallelLength(env_routing_rect, cut_rect) > 0) {
                  adj_par_net_idx_set.insert({env_net_idx});
                }
              }
            }

            if (curr_par_net_idx_set.empty() || adj_par_net_idx_set.empty()) {
              continue;
            }

            Violation violation;
            violation.set_violation_type(ViolationType::kEnclosureEdge);
            violation.set_is_routing(true);
            violation.set_violation_net_set({net_idx});
            violation.set_layer_idx(below_routing_layer_idx);
            violation.set_rect(cut_rect);
            violation.set_required_size(convex_rule.overhang);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
}
}  // namespace idrc
