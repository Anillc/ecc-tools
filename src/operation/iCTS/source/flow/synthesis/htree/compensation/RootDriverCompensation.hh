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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file RootDriverCompensation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-04
 * @brief H-tree root driver compensation pass contracts.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "ValueLattice.hh"

namespace icts {
class HTreeTopologyChar;
struct PatternId;
class Tree;
}  // namespace icts

namespace icts::htree {

struct BufferPatternLibrary;
struct TopologyPatternLibrary;

struct RootDriverCompensationDetail
{
  bool enabled = false;
  bool valid = false;
  std::string method;
  std::string cell_master;
  std::string load_source;
  std::string route_estimator;
  double input_slew_ns = 0.0;
  unsigned load_bucket_idx = 0U;
  double load_cap_pf = 0.0;
  double terminal_pin_cap_pf = 0.0;
  double wire_cap_pf = 0.0;
  double routed_wirelength_um = 0.0;
  std::size_t terminal_count = 0U;
  double clock_period_ns = 0.0;
  double cell_delay_ns = 0.0;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double cell_power_w = 0.0;
};

struct RootDriverCompensationOptions
{
  bool enabled = false;
  double input_slew_ns = 0.0;
  double clock_period_ns = 0.0;
  UniformValueLattice cap_lattice;
  std::string fallback_cell_master;
};

struct RootDriverCompensationStats
{
  bool enabled = false;
  std::string method;
  double input_slew_ns = 0.0;
  double clock_period_ns = 0.0;
  std::string load_source;
  std::size_t compensated_candidate_count = 0U;
  std::size_t unique_direct_lookup_count = 0U;
  std::size_t cache_hit_count = 0U;
  std::size_t load_resolution_count = 0U;
  std::size_t load_resolution_cache_hit_count = 0U;
  std::size_t load_resolution_failure_count = 0U;
  std::size_t flute_route_estimate_count = 0U;
  std::size_t fallback_route_estimate_count = 0U;
  double load_resolution_runtime_ms = 0.0;
  double total_runtime_ms = 0.0;
};

class RootDriverCompensationPass
{
 public:
  explicit RootDriverCompensationPass(RootDriverCompensationOptions options);
  ~RootDriverCompensationPass();

  RootDriverCompensationPass(const RootDriverCompensationPass&) = delete;
  auto operator=(const RootDriverCompensationPass&) -> RootDriverCompensationPass& = delete;
  RootDriverCompensationPass(RootDriverCompensationPass&&) noexcept;
  auto operator=(RootDriverCompensationPass&&) noexcept -> RootDriverCompensationPass&;

  auto beginCandidateBuild() -> void;
  auto apply(std::vector<HTreeTopologyChar>& entries, const TopologyPatternLibrary& topology_library,
             const BufferPatternLibrary& segment_pattern_library, const Tree& topology) -> void;
  auto evaluate(PatternId pattern_id, const TopologyPatternLibrary& topology_library, const BufferPatternLibrary& segment_pattern_library,
                const Tree& topology) -> RootDriverCompensationDetail;
  auto get_stats() const -> const RootDriverCompensationStats&;

 private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};

}  // namespace icts::htree
