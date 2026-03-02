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

void RuleValidator::verifySameLayerCutSpacing(RVCluster& rv_cluster)
{
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();

  std::map<int32_t, std::vector<std::pair<BGRectInt, int32_t>>> routing_cut_net_map;
  std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> cut_bg_rtree_map;
  // preprocess for cut rect, do not merge
  {
    for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
      if (!drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        routing_cut_net_map[drc_shape->get_layer_idx()].push_back({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (!drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        routing_cut_net_map[drc_shape->get_layer_idx()].push_back({DRCUTIL.convertToBGRectInt(drc_shape->get_rect()), drc_shape->get_net_idx()});
      }
    }

    for (auto& [layer_idx, rect_net_pairs] : routing_cut_net_map) {
      cut_bg_rtree_map[layer_idx] = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>(rect_net_pairs);
    }
  }

  for (auto& [cut_layer_idx, rect_net_pairs] : routing_cut_net_map) {
    std::vector<Violation> layer_violations;
    int32_t routing_layer_idx = -1;
    {
      std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
      routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    }
    CutLayer& cut_layer = cut_layer_list[cut_layer_idx];
    SameLayerCutSpacingRule& same_layer_cut_spacing_rule = cut_layer.get_same_layer_cut_spacing_rule();
    int32_t curr_spacing = same_layer_cut_spacing_rule.curr_spacing;
    int32_t curr_prl_spacing = same_layer_cut_spacing_rule.curr_prl_spacing;
    int32_t curr_prl = -1 * same_layer_cut_spacing_rule.curr_prl;

    for (auto& [cut_bg_rect, net_idx] : rect_net_pairs) {
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_bg_rect);
      PlanarRect checking_region_vertical = DRCUTIL.getEnlargedRect(cut_rect, curr_prl, curr_prl_spacing);
      PlanarRect checking_region_horizontal = DRCUTIL.getEnlargedRect(cut_rect, curr_prl_spacing, curr_prl);
      std::vector<std::pair<BGRectInt, int32_t>> bg_rect_net_pair_list;
      {
        PlanarRect check_rect = DRCUTIL.getEnlargedRect(cut_rect, std::max({curr_spacing, curr_prl, curr_prl_spacing}));
        cut_bg_rtree_map[cut_layer_idx].query(bgi::intersects(DRCUTIL.convertToBGRectInt(check_rect)), std::back_inserter(bg_rect_net_pair_list));
      }
      for (auto& [bg_env_rect, env_net_idx] : bg_rect_net_pair_list) {
        PlanarRect env_rect = DRCUTIL.convertToPlanarRect(bg_env_rect);
        if (DRCUTIL.isClosedOverlap(cut_rect, env_rect)) {
          continue;
        }
        bool use_prl_spacing = false, use_spaing = false;
        use_prl_spacing = DRCUTIL.isOpenOverlap(checking_region_horizontal, env_rect) || DRCUTIL.isOpenOverlap(checking_region_vertical, env_rect);
        use_spaing = DRCUTIL.getEuclideanDistance(cut_rect, env_rect) < curr_spacing;
        if (!use_prl_spacing && !use_spaing) {
          continue;
        }
        Violation violation;
        violation.set_violation_type(ViolationType::kSameLayerCutSpacing);
        violation.set_is_routing(true);
        violation.set_violation_net_set({net_idx, env_net_idx});
        violation.set_layer_idx(routing_layer_idx);
        violation.set_rect(DRCUTIL.getSpacingRect(cut_rect, env_rect));
        violation.set_required_size(use_prl_spacing ? curr_prl_spacing : curr_spacing);
        layer_violations.push_back(std::move(violation));
      }
    }

    // merge same net violation
    {
      if (layer_violations.size() > 1) {
        std::sort(layer_violations.begin(), layer_violations.end(), [](const Violation& a, const Violation& b) {
          if (a.get_violation_net_set() != b.get_violation_net_set())
            return a.get_violation_net_set() < b.get_violation_net_set();

          const auto& ra = a.get_rect();
          const auto& rb = b.get_rect();
          if (ra.get_ll_x() != rb.get_ll_x())
            return ra.get_ll_x() < rb.get_ll_x();
          if (ra.get_ur_x() != rb.get_ur_x())
            return ra.get_ur_x() > rb.get_ur_x();
          if (ra.get_ll_y() != rb.get_ll_y())
            return ra.get_ll_y() < rb.get_ll_y();
          return ra.get_ur_y() > rb.get_ur_y();
        });

        std::vector<Violation> results;
        results.reserve(layer_violations.size());
        std::vector<const Violation*> active_set;

        for (const auto& v : layer_violations) {
          bool is_redundant = false;
          const auto& cur_r = v.get_rect();

          if (!active_set.empty() && active_set.back()->get_violation_net_set() != v.get_violation_net_set()) {
            active_set.clear();
          }

          active_set.erase(
              std::remove_if(active_set.begin(), active_set.end(), [&](const Violation* p) { return p->get_rect().get_ur_x() < cur_r.get_ll_x(); }),
              active_set.end());

          for (const auto* p_active : active_set) {
            if (DRCUTIL.isInside(p_active->get_rect(), cur_r)) {
              is_redundant = true;
              break;
            }
          }

          if (!is_redundant) {
            results.push_back(std::move(const_cast<Violation&>(v)));
            active_set.push_back(&results.back());
          }
        }
        layer_violations = std::move(results);
      }

      auto& final_list = rv_cluster.get_violation_list();
      final_list.insert(final_list.end(), std::make_move_iterator(layer_violations.begin()), std::make_move_iterator(layer_violations.end()));
    }
  }
}

}  // namespace idrc
