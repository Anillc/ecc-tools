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

void RuleValidator::verifyEndOfLineSpacing(RVCluster& rv_cluster)
{
  static constexpr Orientation kOrients[] = {Orientation::kNorth, Orientation::kWest, Orientation::kSouth, Orientation::kEast};

  struct EolInfo
  {
    int32_t pre_length, curr_length, post_length;
    Segment<PlanarCoord> pre_seg, curr_seg, post_seg;
    Orientation orient = Orientation::kNone;
    Orientation pre_orient = Orientation::kNone;
    Orientation post_orient = Orientation::kNone;
  };

  std::map<int32_t, std::map<PlanarRect, EolInfo, CmpPlanarRectByXASC>> global_eol_info_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, const EolInfo*>, bgi::quadratic<16>>> eol_rtree_map;

  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> shape_bg_rtree_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> cut_bg_rtree_map;
  std::map<int32_t, bgi::rtree<BGRectInt, bgi::quadratic<16>>> obs_bg_rtree_map;
  std::map<int32_t, std::vector<GTLPolySetInt>> layer_connected_components;
  std::map<int32_t, std::vector<std::map<Orientation, std::vector<PlanarRect>>>> layer_component_edge;

  std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>> layer_seg_EOL_length;
  // for net_id query
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> routing_bg_rtree_map;

  // preprocess
  {
    std::map<int32_t, GTLPolySetInt> layer_polyset_map;
    std::map<int32_t, std::vector<PlanarRect>> layer_rects;
    std::map<int32_t, GTLPolySetInt> layer_obs;
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (drc_shape->get_is_routing()) {
        layer_polyset_map[drc_shape->get_layer_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
        layer_obs[drc_shape->get_layer_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
        routing_bg_rtree_map[drc_shape->get_layer_idx()].insert({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing()) {
        layer_polyset_map[drc_shape->get_layer_idx()] += DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
        routing_bg_rtree_map[drc_shape->get_layer_idx()].insert({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }

    // 还是和之前一样，减去env。
    for (auto& [routing_layer_idx, obs_gtl_poly_set] : layer_obs) {
      std::vector<GTLRectInt> gtl_rects;
      gtl::get_max_rectangles(gtl_rects, obs_gtl_poly_set);
      for (auto& gtl_rect : gtl_rects) {
        obs_bg_rtree_map[routing_layer_idx].insert(DRCUTIL.convertToBGRectInt(gtl_rect));
      }
    }

    for (auto& [routing_layer_idx, global_gtl_poly_set] : layer_polyset_map) {
      std::vector<GTLHolePolyInt> global_hole_poly_list;
      global_gtl_poly_set.get(global_hole_poly_list);
      for (size_t i = 0; i < global_hole_poly_list.size(); i++) {
        GTLHolePolyInt global_hole_poly = global_hole_poly_list[i];
        GTLPolySetInt temp;
        temp += global_hole_poly;
        layer_connected_components[routing_layer_idx].push_back(temp);
        layer_component_edge[routing_layer_idx].push_back(DRCUTIL.getPolyExtEdges(temp));

        std::vector<GTLRectInt> gtl_rect_list;
        gtl::get_max_rectangles(gtl_rect_list, global_hole_poly);
        for (GTLRectInt rect : gtl_rect_list) {
          PlanarRect planar_rect = DRCUTIL.convertToPlanarRect(rect);
          shape_bg_rtree_map[routing_layer_idx].insert({DRCUTIL.convertToBGRectInt(planar_rect), i});
          layer_rects[routing_layer_idx].push_back(planar_rect);
        }
      }
    }

    // build eol info
    for (auto& [routing_layer_idx, global_gtl_poly_set] : layer_polyset_map) {
      std::vector<GTLHolePolyInt> global_hole_poly_list;
      global_gtl_poly_set.get(global_hole_poly_list);
      for (GTLHolePolyInt& global_hole_poly : global_hole_poly_list) {
        std::vector<std::pair<GTLHolePolyInt, bool>> check_hole_pair_list;
        {
          check_hole_pair_list.emplace_back(global_hole_poly, false);
          for (auto iter = global_hole_poly.begin_holes(); iter != global_hole_poly.end_holes(); iter++) {
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
            PlanarCoord& pre_coord = coord_list[getIdx(i - 1, coord_size)];
            PlanarCoord& curr_coord = coord_list[i];
            if (convex_corner_list[getIdx(i - 1, coord_size)] && convex_corner_list[i]) {
              // 检查eol边扩展出去的1个单位，排除单点接触
              Direction direction = DRCUTIL.getDirection(pre_coord, curr_coord);
              PlanarRect start_probe, end_probe;
              if (direction == Direction::kHorizontal) {
                int32_t x_min = std::min(pre_coord.get_x(), curr_coord.get_x());
                int32_t x_max = std::max(pre_coord.get_x(), curr_coord.get_x());
                int32_t y = pre_coord.get_y();
                start_probe = PlanarRect(x_min - 2, y, x_min - 1, y);
                end_probe = PlanarRect(x_max + 1, y, x_max + 2, y);
              } else {
                int32_t y_min = std::min(pre_coord.get_y(), curr_coord.get_y());
                int32_t y_max = std::max(pre_coord.get_y(), curr_coord.get_y());
                int32_t x = pre_coord.get_x();
                start_probe = PlanarRect(x, y_min - 2, x, y_min - 1);
                end_probe = PlanarRect(x, y_max + 1, x, y_max + 2);
              }
              std::vector<std::pair<BGRectInt, int32_t>> overlap_list;
              shape_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(start_probe)), std::back_inserter(overlap_list));
              shape_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(end_probe)), std::back_inserter(overlap_list));
              if (!overlap_list.empty()) {
                continue;
              }
              auto pre_edge = edge_list[getIdx(i - 1, coord_size)];
              auto curr_edge = edge_list[getIdx(i, coord_size)];
              auto post_edge = edge_list[getIdx(i + 1, coord_size)];
              int32_t pre_length = DRCUTIL.getManhattanDistance(pre_edge.get_first(), pre_edge.get_second());
              int32_t cur_length = DRCUTIL.getManhattanDistance(curr_edge.get_first(), curr_edge.get_second());
              int32_t post_length = DRCUTIL.getManhattanDistance(post_edge.get_first(), post_edge.get_second());
              // get eol edge orient
              PlanarRect bbox = DRCUTIL.getBoundingBox({pre_edge.get_first(), pre_edge.get_second(), post_edge.get_first(), post_edge.get_second()});
              Orientation orient = DRCUTIL.getTouchedEdgeOrient(bbox, curr_edge);
              Orientation pre_orient = DRCUTIL.getTouchedEdgeOrient(bbox, pre_edge);
              Orientation post_orient = DRCUTIL.getTouchedEdgeOrient(bbox, post_edge);

              GTLPolySetInt component_polyset;
              component_polyset += global_hole_poly;
              EolInfo eol_info{pre_length, cur_length, post_length, pre_edge, curr_edge, post_edge, orient, pre_orient, post_orient};
              global_eol_info_map[routing_layer_idx][DRCUTIL.getRect(curr_edge)] = eol_info;

              eol_rtree_map[routing_layer_idx].insert(
                  {DRCUTIL.convertToBGRectInt(DRCUTIL.getRect(curr_edge)), &global_eol_info_map[routing_layer_idx][DRCUTIL.getRect(curr_edge)]});
            }
          }
        }
      }
    }

    for (auto& [routing_layer_idx, rects] : layer_rects) {
      if (eol_rtree_map.find(routing_layer_idx) == eol_rtree_map.end()) {
        continue;
      }
      const auto& eol_rtree = eol_rtree_map[routing_layer_idx];
      auto& seg_len_map = layer_seg_EOL_length[routing_layer_idx];

      for (PlanarRect& rect : rects) {
        for (Orientation orient : kOrients) {
          PlanarRect orient_rect = DRCUTIL.getRect(rect.getOrientEdge(orient));

          std::vector<std::pair<BGRectInt, const EolInfo*>> query_res;
          eol_rtree.query(bgi::intersects(DRCUTIL.convertToBGRectInt(orient_rect)), std::back_inserter(query_res));

          for (const auto& res : query_res) {
            auto first_rect = res.first;
            if (DRCUTIL.isInside(DRCUTIL.convertToPlanarRect(first_rect), orient_rect)) {
              seg_len_map[orient_rect] = res.second->curr_length;
              break;
            }
          }
        }
      }
    }

    // build cut shapes
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        cut_bg_rtree_map[drc_shape->get_layer_idx()].insert(std::make_pair(DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()));
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        cut_bg_rtree_map[drc_shape->get_layer_idx()].insert(std::make_pair(DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()));
      }
    }
  }
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, std::vector<int32_t>>& routing_to_adjacent_cut_map = DRCDM.getDatabase().get_routing_to_adjacent_cut_map();

  // check each eol struct
  std::map<PlanarRect, std::vector<Violation>, CmpPlanarRectByXASC> edge_violation_map;
  for (auto& [routing_layer_idx, eol_rect_map] : global_eol_info_map) {
    std::vector<EndOfLineSpacingRule>& end_of_line_spacing_rule_list = routing_layer_list[routing_layer_idx].get_end_of_line_spacing_rule_list();
    for (auto& [eol_edge_rect, eol_info] : eol_rect_map) {
      PlanarRect eol_rect = DRCUTIL.getBoundingBox(
          {eol_info.pre_seg.get_first(), eol_info.pre_seg.get_second(), eol_info.post_seg.get_first(), eol_info.post_seg.get_second()});
      Direction direction = DRCUTIL.getDirection(eol_info.curr_seg.get_first(), eol_info.curr_seg.get_second());

      // to be checked spacing rects
      std::vector<std::pair<BGRectInt, int32_t>> env_checking_poly_list;
      // par left and right neighbors
      std::vector<std::pair<BGRectInt, int32_t>> env_routing_poly_list;
      // enclosed cut rects
      std::vector<std::pair<BGRectInt, int32_t>> env_cut_bg_net_list;
      {
        bool need_cut_shape = false;
        int32_t max_eol_spacing = 0;
        int32_t max_ete_spacing = 0;
        int32_t max_eol_width = 0;
        int32_t max_par_spacing = 0;
        int32_t max_par_within = 0;
        int32_t max_eol_within = 0;
        for (const EndOfLineSpacingRule& eol_rule : end_of_line_spacing_rule_list) {
          if (eol_rule.has_enclose_cut) {
            need_cut_shape = true;
          }
          max_eol_within = std::max(max_eol_within, eol_rule.eol_within);
          max_eol_spacing = std::max(max_eol_spacing, eol_rule.eol_spacing);
          max_eol_width = std::max(max_eol_width, eol_rule.eol_width);
          if (eol_rule.has_ete) {
            max_ete_spacing = std::max(max_ete_spacing, eol_rule.ete_spacing);
          }
          if (eol_rule.has_subtrace_eol_width) {
            // 应该找到原来polygon， 再计算不同部分的subtrace长度， 但是现在先简单处理成curr_length
            max_par_spacing = std::max(max_par_spacing, eol_rule.par_spacing - eol_info.curr_length);
          } else {
            max_par_spacing = std::max(max_par_spacing, eol_rule.par_spacing);
          }
          max_par_within = std::max(max_par_within, eol_rule.par_within);
        }

        if (need_cut_shape) {
          std::vector<int32_t>& cut_layer_idx_list = routing_to_adjacent_cut_map[routing_layer_idx];
          int32_t cut_layer_idx = *std::min_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
          cut_bg_rtree_map[cut_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(eol_rect)), std::back_inserter(env_cut_bg_net_list));
        }

        PlanarRect max_check_rect = DRCUTIL.getRect(eol_info.curr_seg);
        max_check_rect = DRCUTIL.getEnlargedPartRect(max_check_rect, eol_info.orient, std::max(max_eol_spacing, max_ete_spacing));

        PlanarRect max_par_rect = DRCUTIL.getRect(eol_info.curr_seg);

        int32_t hori_enlargement = max_par_spacing;
        int32_t vert_enlargement = std::max(max_par_within, max_eol_within);
        if (direction == Direction::kHorizontal) {
          max_par_rect = DRCUTIL.getEnlargedRect(max_par_rect, hori_enlargement, vert_enlargement);
          max_check_rect = DRCUTIL.getEnlargedRect(max_check_rect, max_eol_within, 0);
        } else {
          max_par_rect = DRCUTIL.getEnlargedRect(max_par_rect, vert_enlargement, hori_enlargement);
          max_check_rect = DRCUTIL.getEnlargedRect(max_check_rect, 0, max_eol_within);
        }
        PlanarRect merge_rect = DRCUTIL.getBoundingBox({max_check_rect, max_par_rect});
        std::vector<std::pair<BGRectInt, int32_t>> merge_rects;
        shape_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(merge_rect)), std::back_inserter(merge_rects));
        env_checking_poly_list.reserve(merge_rects.size());
        env_routing_poly_list.reserve(merge_rects.size());
        for (auto& [bg_rect, idx] : merge_rects) {
          PlanarRect rect = DRCUTIL.convertToPlanarRect(bg_rect);
          if (DRCUTIL.isOpenOverlap(rect, max_check_rect)) {
            env_checking_poly_list.push_back({bg_rect, idx});
          }
          if (DRCUTIL.isOpenOverlap(rect, max_par_rect)) {
            env_routing_poly_list.push_back({bg_rect, idx});
          }
        }
      }
      if (env_checking_poly_list.empty()) {
        continue;
      }

      for (const EndOfLineSpacingRule& eol_rule : end_of_line_spacing_rule_list) {
        int32_t par_spacing = 0;
        if (eol_rule.has_subtrace_eol_width) {
          par_spacing = std::max(par_spacing, eol_rule.par_spacing - eol_info.curr_length);
        } else {
          par_spacing = std::max(par_spacing, eol_rule.par_spacing);
        }
        PlanarRect pre_rect = DRCUTIL.getRect({eol_info.pre_seg.get_second(), eol_info.pre_seg.get_second()});
        PlanarRect post_rect = DRCUTIL.getRect({eol_info.post_seg.get_first(), eol_info.post_seg.get_first()});

        switch (eol_info.orient) {
          case Orientation::kNorth:
            pre_rect = DRCUTIL.getEnlargedRect(pre_rect, 0, eol_rule.par_within, par_spacing, eol_rule.eol_within);
            post_rect = DRCUTIL.getEnlargedRect(post_rect, par_spacing, eol_rule.par_within, 0, eol_rule.eol_within);
            break;
          case Orientation::kSouth:
            pre_rect = DRCUTIL.getEnlargedRect(pre_rect, par_spacing, eol_rule.eol_within, 0, eol_rule.par_within);
            post_rect = DRCUTIL.getEnlargedRect(post_rect, 0, eol_rule.eol_within, par_spacing, eol_rule.par_within);
            break;
          case Orientation::kWest:
            pre_rect = DRCUTIL.getEnlargedRect(pre_rect, eol_rule.eol_within, 0, eol_rule.par_within, par_spacing);
            post_rect = DRCUTIL.getEnlargedRect(post_rect, eol_rule.eol_within, par_spacing, eol_rule.par_within, 0);
            break;
          case Orientation::kEast:
            pre_rect = DRCUTIL.getEnlargedRect(pre_rect, eol_rule.par_within, par_spacing, eol_rule.eol_within, 0);
            post_rect = DRCUTIL.getEnlargedRect(post_rect, eol_rule.par_within, 0, eol_rule.eol_within, par_spacing);
            break;
          default:
            DRCLOG.error(Loc::current(), "The orientation is error!");
        }

        for (auto& [bg_rect, poly_idx] : env_checking_poly_list) {
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_rect);
          if (DRCUTIL.isClosedOverlap(env_rect, eol_rect)) {
            continue;
          }

          if (eol_info.curr_length >= eol_rule.eol_width) {
            continue;
          }

          // 0: normal eol spacing, 1: ete spacing， 2: par spacing, 3: same metal spacing
          int violation_type = 0;
          PlanarRect par_vio_rect;
          bool is_ete = false;
          PlanarRect env_eol_rect = DRCUTIL.getRect(env_rect.getOrientEdge(DRCUTIL.getOppositeOrientation(eol_info.orient)));
          if (DRCUTIL.exist(layer_seg_EOL_length[routing_layer_idx], env_eol_rect)
              && layer_seg_EOL_length[routing_layer_idx][env_eol_rect] < eol_rule.eol_width) {
            is_ete = true;
          }
          violation_type = is_ete ? 1 : 0;
          PlanarRect check_rect = DRCUTIL.getEnlargedPartRect(eol_edge_rect, eol_info.orient, is_ete ? eol_rule.ete_spacing : eol_rule.eol_spacing);
          if (direction == Direction::kHorizontal) {
            check_rect = DRCUTIL.getEnlargedRect(check_rect, eol_rule.eol_within, 0);
          } else {
            check_rect = DRCUTIL.getEnlargedRect(check_rect, 0, eol_rule.eol_within);
          }
          if (!DRCUTIL.isOpenOverlap(check_rect, env_eol_rect) && !eol_rule.has_same_metal) {
            continue;
          }

          std::vector<BGRectInt> pre_overlap, post_overlap;
          bool pre_par = false, post_par = false;
          if (eol_rule.has_par) {
            for (auto& [par_env_rect, par_poly_idx] : env_routing_poly_list) {
              PlanarRect par_rect = DRCUTIL.convertToPlanarRect(par_env_rect);
              if (DRCUTIL.isClosedOverlap(par_rect, eol_rect)) {
                continue;
              }
              std::map<Orientation, std::vector<PlanarRect>> par_poly_edges = layer_component_edge[routing_layer_idx][par_poly_idx];
              for (PlanarRect& rect : par_poly_edges[DRCUTIL.getOppositeOrientation(eol_info.pre_orient)]) {
                bool find_that = false;
                if (DRCUTIL.isOpenOverlap(rect, pre_rect)) {
                  if (par_poly_idx == poly_idx) {
                    pre_overlap.push_back(par_env_rect);
                  }
                  pre_par = true;
                  find_that = true;
                  par_vio_rect = rect;
                }
                if (find_that) {
                  break;
                }
              }
              for (PlanarRect& rect : par_poly_edges[DRCUTIL.getOppositeOrientation(eol_info.post_orient)]) {
                bool find_that = false;
                if (DRCUTIL.isOpenOverlap(rect, post_rect)) {
                  if (par_poly_idx == poly_idx) {
                    post_overlap.push_back(par_env_rect);
                  }
                  post_par = true;
                  find_that = true;
                  par_vio_rect = rect;
                }
                if (find_that) {
                  break;
                }
              }
            }
            pre_par = eol_info.pre_length >= eol_rule.min_length ? pre_par : false;
            post_par = eol_info.post_length >= eol_rule.min_length ? post_par : false;
            if (eol_rule.has_two_edges) {
              if (!(pre_par && post_par)) {
                continue;
              }
            } else {
              if (!(pre_par || post_par)) {
                continue;
              }
            }

            if (eol_rule.has_same_metal) {
              if (pre_overlap.empty() || post_overlap.empty()) {
                continue;
              }
              bool is_samenet = false;
              for (auto pre_bg_rect : pre_overlap) {
                for (auto post_bg_rect : post_overlap) {
                  PlanarRect pre_par_rect = DRCUTIL.convertToPlanarRect(pre_bg_rect);
                  PlanarRect post_par_rect = DRCUTIL.convertToPlanarRect(post_bg_rect);
                  if (DRCUTIL.isClosedOverlap(pre_par_rect, post_par_rect)) {
                    continue;
                  }

                  PlanarRect space_rect = DRCUTIL.getSpacingRect(pre_par_rect, post_par_rect);
                  if (!DRCUTIL.isOpenOverlap(space_rect, eol_rect)) {
                    continue;
                  }
                  GTLPolySetInt pre_polyset, post_polyset;
                  pre_polyset += layer_connected_components[routing_layer_idx][poly_idx] & DRCUTIL.convertToGTLRectInt(pre_rect);
                  post_polyset += layer_connected_components[routing_layer_idx][poly_idx] & DRCUTIL.convertToGTLRectInt(post_rect);
                  int32_t pre_prl_length = DRCUTIL.getPolysetMaxPRL(pre_polyset, eol_rect);
                  int32_t post_prl_length = DRCUTIL.getPolysetMaxPRL(post_polyset, eol_rect);
                  if (pre_prl_length >= eol_rule.par_within) {
                    continue;
                  }
                  if (post_prl_length >= eol_rule.par_within) {
                    continue;
                  }
                  is_samenet = true;
                  break;
                }
                if (is_samenet) {
                  break;
                }
              }
              if (!is_samenet) {
                continue;
              }
              violation_type = 2;
            }
            if (violation_type == 0) {
              violation_type = 3;
            }
          }

          if (eol_rule.has_enclose_cut) {
            bool is_pass_cut = false;
            for (auto& [cut_bg_rect, cut_idx] : env_cut_bg_net_list) {
              PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_bg_rect);
              if ((DRCUTIL.getEuclideanDistance(cut_rect, eol_edge_rect) < eol_rule.enclosed_dist)
                  && (DRCUTIL.getEuclideanDistance(cut_rect, env_rect)) < eol_rule.cut_to_metal_spacing) {
                is_pass_cut = true;
              }
            }
            if (!is_pass_cut) {
              continue;
            }
          }

          auto check_inside = [&](const auto& target_rect) {
            auto query_box = DRCUTIL.convertToBGRectInt(target_rect);
            auto& rtree = obs_bg_rtree_map[routing_layer_idx];

            for (auto iter = rtree.qbegin(bgi::intersects(query_box)); iter != rtree.qend(); ++iter) {
              auto iter_rect = *iter;
              if (DRCUTIL.isInside(DRCUTIL.convertToPlanarRect(iter_rect), target_rect)) {
                return true;
              }
            }
            return false;
          };

          if (check_inside(eol_edge_rect) && check_inside(env_eol_rect)) {
            continue;
          }

          PlanarRect spacing_rect = DRCUTIL.getSpacingRect(eol_rect, env_rect);
          std::vector<std::pair<BGRectInt, int32_t>> net_in_vio;
          routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(eol_edge_rect)), std::back_inserter(net_in_vio));
          std::set<int32_t> net_list;
          // checking netID
          net_list.insert(net_in_vio.empty() ? -1 : net_in_vio[0].second);
          
          // par violation netID
          if (violation_type >= 2) {
            std::vector<std::pair<BGRectInt, int32_t>> net_in_vio;
            routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(par_vio_rect)), std::back_inserter(net_in_vio));
            if (!net_in_vio.empty()) {
              net_list.insert(net_in_vio[0].second);
            }
          } else {
            std::vector<std::pair<BGRectInt, int32_t>> net_in_vio;
            routing_bg_rtree_map[routing_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(env_eol_rect)), std::back_inserter(net_in_vio));
            if (!net_in_vio.empty()) {
              net_list.insert(net_in_vio[0].second);
            }
          }
          

          Violation violation;
          violation.set_violation_type(ViolationType::kEndOfLineSpacing);
          violation.set_required_size(violation_type == 1 ? eol_rule.ete_spacing : eol_rule.eol_spacing);
          violation.set_is_routing(true);
          violation.set_violation_net_set(net_list);
          violation.set_layer_idx(routing_layer_idx);
          violation.set_rect(spacing_rect);
          edge_violation_map[eol_rect].push_back(violation);
        }
      }
    }
  }
  std::set<Violation, CmpViolation> invalid_violation_set;
  std::set<Violation, CmpViolation> unique_violation;
  for (auto& [edge, violation_list] : edge_violation_map) {
    for (auto& violation : violation_list) {
      unique_violation.insert(violation);
    }
  }
  // 输出required size最大的violation
  for (auto violation : unique_violation) {
    for (auto other_violation : unique_violation) {
      if (violation.get_rect() == other_violation.get_rect()) {
        if (violation.get_required_size() < other_violation.get_required_size()) {
          invalid_violation_set.insert(violation);
        }
      } else if (DRCUTIL.isInside(other_violation.get_rect(), violation.get_rect())) {
        invalid_violation_set.insert(violation);
      }
    }
  }
  for (auto violation : unique_violation) {
    if (!DRCUTIL.exist(invalid_violation_set, violation)) {
      rv_cluster.get_violation_list().push_back(violation);
    }
  }
}

}  // namespace idrc
