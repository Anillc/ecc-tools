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
 * @file LocalLegalization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-17
 * @brief Standalone point-based local legalization solver.
 */
#include "LocalLegalization.hh"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "geometry/Geometry.hh"
#include "logger/Logger.hh"

namespace icts {
namespace {

constexpr long long kForbiddenCost = std::numeric_limits<int>::max() / 4;

bool ContainsPoint(const std::vector<LocalLegalization::PointType>& points, const LocalLegalization::PointType& point)
{
  return std::ranges::find(points, point) != points.end();
}

std::vector<std::size_t> HungarianSolve(const std::vector<std::vector<long long>>& cost_matrix)
{
  const std::size_t row_count = cost_matrix.size();
  const std::size_t col_count = row_count == 0 ? 0 : cost_matrix.front().size();
  if (row_count == 0 || col_count == 0 || row_count > col_count) {
    return {};
  }

  std::vector<long long> u(row_count + 1, 0);
  std::vector<long long> v(col_count + 1, 0);
  std::vector<std::size_t> p(col_count + 1, 0);
  std::vector<std::size_t> way(col_count + 1, 0);

  for (std::size_t i = 1; i <= row_count; ++i) {
    p[0] = i;
    std::size_t col0 = 0;
    std::vector<long long> min_v(col_count + 1, std::numeric_limits<long long>::max());
    std::vector<bool> used(col_count + 1, false);

    do {
      used[col0] = true;
      const std::size_t row0 = p[col0];
      long long delta = std::numeric_limits<long long>::max();
      std::size_t col1 = 0;

      for (std::size_t col = 1; col <= col_count; ++col) {
        if (used[col]) {
          continue;
        }
        const long long current = cost_matrix[row0 - 1][col - 1] - u[row0] - v[col];
        if (current < min_v[col]) {
          min_v[col] = current;
          way[col] = col0;
        }
        if (min_v[col] < delta) {
          delta = min_v[col];
          col1 = col;
        }
      }

      for (std::size_t col = 0; col <= col_count; ++col) {
        if (used[col]) {
          u[p[col]] += delta;
          v[col] -= delta;
        } else {
          min_v[col] -= delta;
        }
      }
      col0 = col1;
    } while (p[col0] != 0);

    do {
      const std::size_t col1 = way[col0];
      p[col0] = p[col1];
      col0 = col1;
    } while (col0 != 0);
  }

  std::vector<std::size_t> assignment(row_count, std::numeric_limits<std::size_t>::max());
  for (std::size_t col = 1; col <= col_count; ++col) {
    if (p[col] != 0) {
      assignment[p[col] - 1] = col - 1;
    }
  }
  return assignment;
}

}  // namespace

LocalLegalization::RegionType LocalLegalization::buildLegalRegion(const RegionType& feasible_region, const RegionType& block_region)
{
  if (feasible_region.empty()) {
    return feasible_region;
  }
  auto legal_region = feasible_region;
  legal_region.subtract(block_region);
  return legal_region;
}

LocalLegalization::Result LocalLegalization::legalize(const Problem& problem)
{
  return legalize(problem, Options{});
}

LocalLegalization::Result LocalLegalization::legalize(const Problem& problem, const Options& options)
{
  Result result;
  result.legalized_points = problem.movable_points;
  if (problem.movable_points.empty()) {
    CTS_LOG_WARNING << "LocalLegalization skipped: movable point set is empty.";
    result.success = true;
    return result;
  }

  const auto legal_region = buildLegalRegion(problem.feasible_region, problem.block_region);
  for (int round = 0; round < std::max(1, options.max_expansion_rounds); ++round) {
    const auto candidate_budget = std::max<std::size_t>(1, options.candidate_budget * static_cast<std::size_t>(round + 1));
    const int local_search_radius = std::max(0, options.local_search_radius * (round + 1));

    std::vector<std::vector<CandidateSite>> candidate_sets;
    candidate_sets.reserve(problem.movable_points.size());

    bool complete = true;
    for (const auto& origin : problem.movable_points) {
      auto candidates = generateCandidates(origin, legal_region, problem.fixed_points, candidate_budget, local_search_radius);
      if (candidates.empty()) {
        complete = false;
        break;
      }
      candidate_sets.push_back(std::move(candidates));
    }

    if (!complete) {
      CTS_LOG_WARNING << "LocalLegalization expansion round " << round
                      << " generated incomplete candidate sets; retrying with wider search.";
      continue;
    }

    auto legalized_points = solveAssignment(problem.movable_points, candidate_sets);
    if (!legalized_points.empty()) {
      result.legalized_points = std::move(legalized_points);
      result.total_displacement = computeTotalDisplacement(problem.movable_points, result.legalized_points);
      result.success = true;
      return result;
    }
  }

  if (options.failure_policy == FailurePolicy::kKeepOriginal) {
    CTS_LOG_WARNING << "LocalLegalization failed to find a legal assignment; keeping original point locations.";
    result.legalized_points = problem.movable_points;
    return result;
  }

  CTS_LOG_ERROR << "LocalLegalization failed to find a legal assignment.";
  return result;
}

LocalLegalization::Result LocalLegalization::legalize(std::vector<PointType>& movable_points, const std::vector<PointType>& fixed_points,
                                                      const RegionType& feasible_region, const RegionType& block_region)
{
  return legalize(movable_points, fixed_points, feasible_region, block_region, Options{});
}

LocalLegalization::Result LocalLegalization::legalize(std::vector<PointType>& movable_points, const std::vector<PointType>& fixed_points,
                                                      const RegionType& feasible_region, const RegionType& block_region,
                                                      const Options& options)
{
  Problem problem;
  problem.movable_points = movable_points;
  problem.fixed_points = fixed_points;
  problem.feasible_region = feasible_region;
  problem.block_region = block_region;

  auto result = legalize(problem, options);
  if (result.success || options.failure_policy == FailurePolicy::kKeepOriginal) {
    movable_points = result.legalized_points;
  }
  return result;
}

std::vector<LocalLegalization::CandidateSite> LocalLegalization::generateCandidates(const PointType& origin, const RegionType& legal_region,
                                                                                    const std::vector<PointType>& fixed_points,
                                                                                    std::size_t candidate_budget, int local_search_radius)
{
  std::vector<CandidateSite> candidates;
  candidates.reserve(candidate_budget);

  appendCandidate(candidates, origin, legal_region, fixed_points, candidate_budget);

  std::vector<PointType> seeds;
  if (legal_region.empty()) {
    seeds.push_back(origin);
  } else {
    if (auto nearest = geometry::ProjectNearest(legal_region, origin); nearest.has_value()) {
      seeds.push_back(*nearest);
      appendCandidate(candidates, *nearest, legal_region, fixed_points, candidate_budget);
    }
    for (const auto& rect : legal_region.rects()) {
      auto projected = rect.clamp(origin);
      seeds.push_back(projected);
      appendCandidate(candidates, projected, legal_region, fixed_points, candidate_budget);
    }
  }

  auto boundary_candidates = enumerateBoundaryBreakpoints(origin, legal_region, fixed_points, candidate_budget);
  for (const auto& candidate : boundary_candidates) {
    seeds.push_back(candidate.point);
    appendCandidate(candidates, candidate.point, legal_region, fixed_points, candidate_budget);
  }

  for (const auto& seed : seeds) {
    auto neighbor_candidates = enumerateProjectedNeighbors(seed, legal_region, fixed_points, local_search_radius, candidate_budget);
    for (const auto& candidate : neighbor_candidates) {
      appendCandidate(candidates, candidate.point, legal_region, fixed_points, candidate_budget);
    }
  }

  std::sort(candidates.begin(), candidates.end(), [&](const CandidateSite& lhs, const CandidateSite& rhs) {
    const auto lhs_dist = geometry::Manhattan(origin, lhs.point);
    const auto rhs_dist = geometry::Manhattan(origin, rhs.point);
    if (lhs_dist != rhs_dist) {
      return lhs_dist < rhs_dist;
    }
    return lhs.point < rhs.point;
  });
  if (candidates.size() > candidate_budget) {
    candidates.resize(candidate_budget);
  }
  return candidates;
}

std::vector<LocalLegalization::CandidateSite> LocalLegalization::enumerateProjectedNeighbors(const PointType& seed,
                                                                                             const RegionType& legal_region,
                                                                                             const std::vector<PointType>& fixed_points,
                                                                                             int max_radius, std::size_t candidate_budget)
{
  std::vector<CandidateSite> candidates;
  candidates.reserve(candidate_budget);

  for (int radius = 1; radius <= max_radius && candidates.size() < candidate_budget; ++radius) {
    for (int dx = -radius; dx <= radius && candidates.size() < candidate_budget; ++dx) {
      const int dy = radius - std::abs(dx);
      appendCandidate(candidates, PointType(seed.get_x() + dx, seed.get_y() + dy), legal_region, fixed_points, candidate_budget);
      if (dy != 0 && candidates.size() < candidate_budget) {
        appendCandidate(candidates, PointType(seed.get_x() + dx, seed.get_y() - dy), legal_region, fixed_points, candidate_budget);
      }
    }
  }

  return candidates;
}

std::vector<LocalLegalization::CandidateSite> LocalLegalization::enumerateBoundaryBreakpoints(const PointType& origin,
                                                                                              const RegionType& legal_region,
                                                                                              const std::vector<PointType>& fixed_points,
                                                                                              std::size_t candidate_budget)
{
  std::vector<CandidateSite> candidates;
  candidates.reserve(candidate_budget);

  if (legal_region.empty()) {
    return candidates;
  }

  for (const auto& rect : legal_region.rects()) {
    const int clamped_x = std::clamp(origin.get_x(), rect.get_min_x(), rect.get_max_x());
    const int clamped_y = std::clamp(origin.get_y(), rect.get_min_y(), rect.get_max_y());

    appendCandidate(candidates, PointType(rect.get_min_x(), rect.get_min_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(rect.get_min_x(), rect.get_max_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(rect.get_max_x(), rect.get_min_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(rect.get_max_x(), rect.get_max_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(clamped_x, rect.get_min_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(clamped_x, rect.get_max_y()), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(rect.get_min_x(), clamped_y), legal_region, fixed_points, candidate_budget);
    appendCandidate(candidates, PointType(rect.get_max_x(), clamped_y), legal_region, fixed_points, candidate_budget);
  }

  return candidates;
}

std::vector<LocalLegalization::PointType> LocalLegalization::solveAssignment(const std::vector<PointType>& movable_points,
                                                                             const std::vector<std::vector<CandidateSite>>& candidate_sets)
{
  if (movable_points.empty()) {
    return {};
  }

  std::map<PointType, std::size_t> point_to_site;
  std::vector<PointType> site_points;
  for (const auto& candidate_set : candidate_sets) {
    for (const auto& candidate : candidate_set) {
      if (point_to_site.contains(candidate.point)) {
        continue;
      }
      point_to_site[candidate.point] = site_points.size();
      site_points.push_back(candidate.point);
    }
  }

  if (site_points.size() < movable_points.size()) {
    return {};
  }

  std::vector<std::vector<long long>> cost_matrix(movable_points.size(), std::vector<long long>(site_points.size(), kForbiddenCost));
  for (std::size_t i = 0; i < movable_points.size(); ++i) {
    for (const auto& candidate : candidate_sets[i]) {
      auto site_iter = point_to_site.find(candidate.point);
      if (site_iter == point_to_site.end()) {
        continue;
      }
      cost_matrix[i][site_iter->second] = geometry::Manhattan(movable_points[i], candidate.point);
    }
  }

  auto assignment = HungarianSolve(cost_matrix);
  if (assignment.size() != movable_points.size()) {
    return {};
  }

  std::vector<PointType> legalized_points(movable_points.size(), PointType(-1, -1));
  for (std::size_t i = 0; i < movable_points.size(); ++i) {
    if (assignment[i] >= site_points.size() || cost_matrix[i][assignment[i]] >= kForbiddenCost / 2) {
      return {};
    }
    legalized_points[i] = site_points[assignment[i]];
  }
  return legalized_points;
}

long long LocalLegalization::computeTotalDisplacement(const std::vector<PointType>& original_points,
                                                      const std::vector<PointType>& legalized_points)
{
  long long total_displacement = 0;
  const auto count = std::min(original_points.size(), legalized_points.size());
  for (std::size_t i = 0; i < count; ++i) {
    total_displacement += static_cast<long long>(geometry::Manhattan(original_points[i], legalized_points[i]));
  }
  return total_displacement;
}

void LocalLegalization::appendCandidate(std::vector<CandidateSite>& candidates, const PointType& point, const RegionType& legal_region,
                                        const std::vector<PointType>& fixed_points, std::size_t candidate_budget)
{
  if (!legal_region.empty() && !legal_region.contains(point)) {
    return;
  }
  if (ContainsPoint(fixed_points, point)) {
    return;
  }
  const bool already_exists = std::ranges::any_of(candidates, [&](const auto& candidate) { return candidate.point == point; });
  if (already_exists) {
    return;
  }
  if (candidates.size() >= candidate_budget) {
    return;
  }
  candidates.push_back(CandidateSite{point});
}

}  // namespace icts
