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
  auto& layer_cut_net_rtrees = rv_cluster.get_layer_cut_net_rtrees();

  for (auto& [cut_layer_idx, cut_net_rtree] : layer_cut_net_rtrees) {
    int32_t below_cut_layer_idx = cut_layer_idx - 1;
    if (below_cut_layer_idx < 0) {
      continue;
    }
    auto below_cut_rtree_it = layer_cut_net_rtrees.find(below_cut_layer_idx);
    if (below_cut_rtree_it == layer_cut_net_rtrees.end()) {
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
    for (auto [cut_gtl_rect, net_idx] : cut_net_rtree) {
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_gtl_rect);
      std::vector<std::pair<GTLRectInt, int32_t>> gtl_rect_net_pair_list;
      {
        PlanarRect check_rect = DRCUTIL.getEnlargedRect(cut_rect, std::max({curr_spacing, curr_prl, curr_prl_spacing}));
        below_cut_rtree_it->second.query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)), std::back_inserter(gtl_rect_net_pair_list));
      }
      for (auto& [gtl_env_rect, env_net_idx] : gtl_rect_net_pair_list) {
        if (net_idx == -1 && env_net_idx == -1) {
          continue;
        }
        if (net_idx == env_net_idx) {
          continue;
        }
        PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_env_rect);
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
