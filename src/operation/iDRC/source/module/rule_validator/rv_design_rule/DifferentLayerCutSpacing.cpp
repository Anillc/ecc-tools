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

void RuleValidator::verifyDifferentLayerCutSpacing(RVCluster& rv_cluster)
{
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();

  std::map<int32_t, std::vector<std::pair<BGRectInt, int32_t>>> routing_cut_net_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> cut_bg_rtree_map;
  // preprocess for cut rect, do not merge
  {
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        routing_cut_net_map[drc_shape->get_layer_idx()].push_back({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (!drc_shape->get_is_routing()) {
        routing_cut_net_map[drc_shape->get_layer_idx()].push_back({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }

    for (auto& [layer_idx, rect_net_pairs] : routing_cut_net_map) {
      cut_bg_rtree_map[layer_idx] = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>(rect_net_pairs);
    }
  }
  for (auto& [cut_layer_idx, rect_net_pairs] : routing_cut_net_map) {
    int32_t below_cut_layer_idx = cut_layer_idx - 1;
    if (below_cut_layer_idx < 0) {
      continue;
    }
    int32_t routing_layer_idx = -1;
    {
      std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[below_cut_layer_idx];
      routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    }
    CutLayer& cut_layer = cut_layer_list[cut_layer_idx];
    DifferentLayerCutSpacingRule& different_layer_cut_spacing_rule = cut_layer.get_different_layer_cut_spacing_rule();
    int32_t curr_spacing = different_layer_cut_spacing_rule.below_spacing;
    int32_t curr_prl_spacing = different_layer_cut_spacing_rule.below_prl_spacing;
    int32_t curr_prl = different_layer_cut_spacing_rule.below_prl;
    for (auto& [cut_bg_rect, net_idx] : rect_net_pairs) {
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_bg_rect);
      std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_pair_list;
      {
        PlanarRect check_rect = DRCUTIL.getEnlargedRect(cut_rect, std::max({curr_spacing, curr_prl, curr_prl_spacing}));
        cut_bg_rtree_map[below_cut_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)), std::back_inserter(bg_rect_net_pair_list));
      }
      for (auto& [bg_env_rect, env_net_idx] : bg_rect_net_pair_list) {
        if (net_idx == -1 && env_net_idx == -1) {
          continue;
        }
        if (net_idx == env_net_idx) {
          continue;
        }
        PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
        if (DRCUTIL.isClosedOverlap(cut_rect, env_rect)) {
          continue;
        }
        int32_t prl = DRCUTIL.getParallelLength(cut_rect, env_rect);
        int32_t required_size = prl > curr_prl ? curr_prl_spacing : curr_spacing;
        int32_t distance = DRCUTIL.getEuclideanDistance(cut_rect, env_rect);
        if (distance >= required_size) {
          continue;
        }
        Violation violation;
        violation.set_violation_type(ViolationType::kDifferentLayerCutSpacing);
        violation.set_is_routing(true);
        violation.set_violation_net_set({net_idx, env_net_idx});
        violation.set_layer_idx(routing_layer_idx);
        violation.set_rect(DRCUTIL.getSpacingRect(cut_rect, env_rect));
        violation.set_required_size(required_size);
        rv_cluster.get_violation_list().push_back(std::move(violation));
      }
    }
  }
}

}  // namespace idrc
