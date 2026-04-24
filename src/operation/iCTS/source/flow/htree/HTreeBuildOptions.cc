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
/**
 * @file HTreeBuildOptions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Resolves H-tree build options against characterization boundaries.
 */

#include <algorithm>
#include <optional>

#include "CharBuilder.hh"
#include "ValueLattice.hh"
#include "config/Config.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"

namespace icts::htree_builder {

auto CoveringBoundaryIndex(double value, const UniformValueLattice& lattice) -> std::optional<unsigned>
{
  if (value <= 0.0 || !lattice.isValid()) {
    return std::nullopt;
  }
  if (value > lattice.maxValue() + kValueLatticeEpsilon) {
    return lattice.steps() + 1U;
  }
  return std::clamp(lattice.coveringIndex(value), 1U, lattice.steps());
}

auto ResolveBuildOptions(const HTreeBuilder::BuildOptions& options, const CharBuilder& char_builder) -> ResolvedBuildOptions
{
  ResolvedBuildOptions resolved;
  resolved.force_branch_buffer = options.force_branch_buffer.value_or(CONFIG_INST.is_force_branch_buffer());

  if (options.min_top_input_slew_ns.has_value() && *options.min_top_input_slew_ns > 0.0) {
    resolved.min_top_input_slew_ns = options.min_top_input_slew_ns;
    resolved.top_input_slew_covering_idx = CoveringBoundaryIndex(*options.min_top_input_slew_ns, char_builder.get_slew_lattice());
  }

  return resolved;
}

auto HasBoundaryConstraints(const ResolvedBuildOptions& options) -> bool
{
  return options.top_input_slew_covering_idx.has_value();
}

}  // namespace icts::htree_builder
