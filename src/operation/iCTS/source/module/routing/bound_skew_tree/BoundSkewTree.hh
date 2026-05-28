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
 * @file BoundSkewTree.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Bound-skew tree construction interface.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "BSTTypes.hh"
#include "Components.hh"
#include "GeomCalc.hh"

namespace icts::bst {
/**
 * @brief Tool namespace
 *
 */
using Geom = GeomCalc;
/**
 * @brief bound skew tree
 *
 */
class BoundSkewTree
{
 public:
  static constexpr double kHalfFactor = 0.5;

  BoundSkewTree(std::vector<std::unique_ptr<Area>> load_areas, const BSTParameters& parameters, const TopoType& topo_type);
  BoundSkewTree(std::vector<std::unique_ptr<Area>> owned_areas, Area* root, const BSTParameters& parameters);
  ~BoundSkewTree() = default;

  auto run() -> void;
  auto get_root() const -> Area* { return _root; }

  auto set_root_guide(const double& x_coord, const double& y_coord) -> void { _root_guide = Point(x_coord, y_coord, 0, 0, 0); }

  auto set_pattern(const RCPattern& pattern) -> void { _pattern = pattern; }

 private:
  struct KMeansConfig
  {
    size_t cluster_count = 0;
    uint32_t seed = 0;
    size_t max_iter = 10;
  };

  struct MergeAreas
  {
    Area* parent = nullptr;
    Area* left = nullptr;
    Area* right = nullptr;
  };

  struct EmbeddingStep
  {
    Area* parent = nullptr;
    Area* child = nullptr;
    size_t side = 0;
  };

  enum class BalanceRefAxis : size_t
  {
    kX = 0,
    kY = 1,
  };

  struct JoiningSegmentDelayQuery
  {
    size_t joining_region_side = 0;
    size_t segment_side = 0;
    size_t point_index = 0;
    size_t timing_type = 0;
  };

  struct SideDelay
  {
    double left = 0.0;
    double right = 0.0;

    auto get(const size_t& side) const -> double { return side == kLeft ? left : right; }
  };

  struct BalancePointQuery
  {
    Point first_point;
    Point second_point;
    size_t timing_type = 0;
    BalanceRefAxis balance_ref_axis = BalanceRefAxis::kX;
    RCPattern pattern = RCPattern::kHV;
  };

  struct BalancePointResult
  {
    double distance_to_first = 0.0;
    double distance_to_second = 0.0;
    Point balance_point;
  };

  struct MergeDistances
  {
    double distance_to_first = 0.0;
    double distance_to_second = 0.0;
  };

  struct MergeRegionSpan
  {
    size_t side = 0;
    size_t left_index = 0;
    size_t right_index = 0;
  };

  template <typename T>
  struct SideState
  {
    T left{};
    T right{};

    auto forSide(const size_t side) -> T& { return side == kLeft ? left : right; }
    auto forSide(const size_t side) const -> const T& { return side == kLeft ? left : right; }
  };

  template <typename T>
  struct EndState
  {
    T head{};
    T tail{};

    auto forEnd(const size_t end_side) -> T& { return end_side == kHead ? head : tail; }
    auto forEnd(const size_t end_side) const -> const T& { return end_side == kHead ? head : tail; }
  };

  template <typename T>
  struct TimingState
  {
    T min{};
    T max{};

    auto forTiming(const size_t timing_type) -> T& { return timing_type == kMin ? min : max; }
    auto forTiming(const size_t timing_type) const -> const T& { return timing_type == kMin ? min : max; }
  };

  struct AxisDelayFactor
  {
    double horizontal = 0.0;
    double vertical = 0.0;
  };

  /**
   * @brief match
   *
   */
  using CostFunc = std::function<double(Area*, Area*)>;
  auto getBestMatch(const CostFunc& cost_func) const -> Match;
  auto mergeCost(Area* left, Area* right) const -> double;
  static auto distanceCost(Area* left, Area* right) -> double;
  /**
   * @brief topology
   *
   */
  auto merge(Area* left, Area* right) -> Area*;
  auto makeArea(const size_t& area_id) -> Area*;
  auto areaReset() -> void;
  static auto resetPointValues(Area* root) -> void;
  auto biPartition() -> void;
  auto buildBiPartitionTree(const std::vector<Area*>& areas) -> Area*;
  static auto octagonDivide(std::vector<Area*>& areas) -> std::pair<std::vector<Area*>, std::vector<Area*>>;
  static auto calcOctagon(const std::vector<Area*>& areas) -> std::vector<Point>;
  static auto areaOnOctagonBound(const std::vector<Area*>& areas, const std::vector<Point>& octagon) -> std::vector<Area*>;
  auto biCluster() -> void;
  auto buildBiClusterTree(const std::vector<Area*>& areas) -> Area*;
  static auto kMeansPlus(const std::vector<Area*>& areas, const KMeansConfig& config) -> std::vector<std::vector<Area*>>;
  static auto calcAreasCenter(const std::vector<Area*>& areas) -> Point;
  /**
   * @brief flow require
   *
   */
  // main flow
  auto bottomUp() -> void;
  auto bottomUpAllPairBased() -> void;
  auto bottomUpTopoBased() -> void;
  auto processBottomUpTopology() -> void;
  auto topDown() -> void;
  auto embedTree() const -> void;
  auto updateEmbeddedNodeTiming(Area* current) const -> void;

  // main interface
  auto merge(Area* parent, Area* left, Area* right) -> void;
  auto calcJoiningSegment(const MergeAreas& merge_areas) -> void;
  auto processJoiningSegment(Area* current_area) -> void;
  auto constructMergeRegion(const MergeAreas& merge_areas) -> void;

  // Joining Segment
  auto initSide() -> void;
  auto calcJoiningSegment(Area* current_area, Line& left_line, Line& right_line) -> void;
  auto calcJoiningSegmentDelay(Area* left_area, Area* right_area) -> void;
  auto updateJoiningSegment(Area* current_area, Line& left_line, Line& right_line, PointPair closest_pair) -> void;
  auto addJoiningSegmentPoints(const MergeAreas& merge_areas) -> void;
  auto delayFromJoiningSegment(const JoiningSegmentDelayQuery& query, const SideDelay& delay_from) const -> double;

  // Joining Region
  auto calcJoiningRegion(const MergeAreas& merge_areas) -> void;
  auto calcJoiningRegionEndpoints(const Area& current_area) -> void;
  auto calcNonManhattanJoiningRegionEndpoints(const MergeAreas& merge_areas) -> void;
  auto addTurnPoint(const size_t& side, const size_t& point_index, const size_t& timing_type, const SideDelay& delay_from) -> void;
  auto addFeasibleMergeSegmentToJoiningRegion() -> void;

  // Joining Corner
  auto calcJoiningRegionCorner(const Area& current_area) -> void;
  auto joiningRegionCornerExists(const size_t& end_side) const -> bool;

  // Balance Point
  auto calcBalancePoint(const Area& current_area) -> void;
  auto calcBalanceBetweenPoints(const BalancePointQuery& query, BalancePointResult& result) const -> void;
  auto calcBalancePointOnLine(const BalancePointQuery& query, BalancePointResult& result) const -> void;
  auto calcBalancePointOffLine(const BalancePointQuery& query, BalancePointResult& result) const -> void;
  static auto calcMergeDist(const double& unit_resistance, const double& unit_capacitance, const double& cap_load_1, const double& delay_1,
                            const double& cap_load_2, const double& delay_2, const double& total_distance) -> MergeDistances;
  static auto calcPointCoordOnLine(const Point& first_point, const Point& second_point, const double& distance_to_first,
                                   const double& distance_to_second, Point& point) -> void;
  auto calcXBalancePosition(const double& delay_1, const double& delay_2, const double& cap_load_1, const double& cap_load_2,
                            const double& horizontal_distance, const double& vertical_distance, BalanceRefAxis balance_ref_axis) const
      -> double;
  auto calcYBalancePosition(const double& delay_1, const double& delay_2, const double& cap_load_1, const double& cap_load_2,
                            const double& horizontal_distance, const double& vertical_distance, BalanceRefAxis balance_ref_axis) const
      -> double;
  static auto calcManhattanDistanceComponents(const Point& first_point, const Point& second_point) -> std::pair<double, double>;

  // Feasible Merging Section
  auto calcFeasibleMergeSegmentPoints(const Area& current_area) -> void;
  auto calcFeasibleMergeSegmentOnLine(const Area& current_area, Point& point, const Point& reference_point, const size_t& end_side) -> bool;
  auto calcFeasibleMergeSegmentBetweenPoints(const Point& high_skew_point, const Point& low_skew_point, Point& feasible_merge_point) const
      -> void;
  auto hasFeasibleMergeSegmentOnJoiningRegion() const -> bool;

  // Merging Region
  auto constructFeasibleMergeRegion(Area* parent) const -> void;
  auto isJoiningRegionLine() const -> bool;
  auto addMergeRegionBetweenJoiningSegments(Area* current_area, const size_t& end_side) const -> void;
  auto addMergeRegionOnJoiningSegment(Area* current_area, const size_t& side) const -> void;
  auto calcMergeRegionLeftIndex(const size_t& side) const -> size_t;
  auto calcMergeRegionSpan(const size_t& side, const size_t& left_index) const -> MergeRegionSpan;
  auto appendMergeRegionPointsOnSegment(Area* current_area, const MergeRegionSpan& merge_region_span) const -> void;
  auto addMergeRegionPointFromJoiningRegion(Area* current_area, const size_t& side, const size_t& point_index) const -> void;
  auto calcSkewSlope(const Area& current_area) const -> double;

  auto constructInfeasibleMergeRegion(Area* parent) const -> void;
  auto calcMinSkewSection(Area* current_area) const -> void;
  auto calcDetourEdgeLength(Area* current_area) const -> void;
  auto refineMergeRegionDelay(Area* current_area) const -> void;

  auto constructTransformedRectMergeRegion(Area* current_area) const -> void;

  // Embedding
  static auto embedChild(const EmbeddingStep& embedding_target) -> void;
  static auto isTransformedRectArea(Area* current_area) -> bool;
  static auto isManhattanArea(Area* current_area) -> bool;
  static auto mergeRegionToTransformedRect(const Region& merge_region, TransformedRect& transformed_rect) -> void;

  // basic function
  static auto calcAreaLineType(const Area& current_area) -> LineType;
  static auto calcConvexHull(Area* current_area) -> void;
  static auto calcJoiningRegionArea(const Line& first_line, const Line& second_line) -> double;

  static auto locateBoundarySegment(Area* current_area, Point& point, Line& boundary_segment) -> void;
  auto calcSimplePointDelays(Point& point, Line& boundary_segment) const -> bool;
  auto calcSegmentPointDelays(Point& point, Line& boundary_segment) const -> void;
  auto calcPointDelays(const Area& current_area, Point& point, Line& boundary_segment) const -> void;
  auto updatePointDelaysByEndSide(const Area& current_area, const size_t& end_side, Point& point) const -> void;
  auto calcIrregularPointDelays(const Area& current_area, Point& point, Line& boundary_segment) const -> void;
  auto pointDelayIncrease(const Point& lhs_point, const Point& rhs_point, const double& cap_load, const RCPattern& pattern) const -> double;
  auto pointDelayIncrease(const Point& lhs_point, const Point& rhs_point, const double& length, const double& cap_load,
                          const RCPattern& pattern) const -> double;
  auto calcDelayIncrease(const double& horizontal_length, const double& vertical_length, const double& cap_load,
                         const RCPattern& pattern) const -> double;
  static auto pointSkew(const Point& point) -> double;
  auto getJoiningRegionLine(const size_t& side) const -> Line;
  auto getJoiningSegmentLine(const size_t& side) const -> Line;
  auto setJoiningRegionLine(const size_t& side, const Line& line) -> void;
  auto setJoiningSegmentLine(const size_t& side, const Line& line) -> void;
  static auto otherSide(const size_t& side) -> size_t { return side == kLeft ? kRight : kLeft; }
  static auto oppositeEnd(const size_t& end_side) -> size_t { return end_side == kHead ? kTail : kHead; }
  auto joiningRegionPoints(const size_t& side) -> Points& { return _joining_region.forSide(side); }
  auto joiningRegionPoints(const size_t& side) const -> const Points& { return _joining_region.forSide(side); }
  auto joiningSegmentPoints(const size_t& side) -> Points& { return _joining_segment.forSide(side); }
  auto joiningSegmentPoints(const size_t& side) const -> const Points& { return _joining_segment.forSide(side); }
  auto mergeSegment(const size_t& side) -> TransformedRect& { return _merge_segment.forSide(side); }
  auto mergeSegment(const size_t& side) const -> const TransformedRect& { return _merge_segment.forSide(side); }
  auto balancePoints(const size_t& end_side) -> Points& { return _balance_points.forEnd(end_side); }
  auto balancePoints(const size_t& end_side) const -> const Points& { return _balance_points.forEnd(end_side); }
  auto joiningCornerPoint(const size_t& end_side) -> Point& { return _joining_corner.forEnd(end_side); }
  auto joiningCornerPoint(const size_t& end_side) const -> const Point& { return _joining_corner.forEnd(end_side); }
  auto feasibleMergeSegmentPoints(const size_t& end_side) -> Points& { return _feasible_merge_segment_points.forEnd(end_side); }
  auto feasibleMergeSegmentPoints(const size_t& end_side) const -> const Points& { return _feasible_merge_segment_points.forEnd(end_side); }
  static auto pointAt(Points& points, const size_t& point_index) -> Point& { return points.at(point_index); }
  static auto pointAt(const Points& points, const size_t& point_index) -> const Point& { return points.at(point_index); }
  static auto linePoint(Line& line, const size_t& end_side) -> Point& { return line.at(end_side); }
  static auto linePoint(const Line& line, const size_t& end_side) -> const Point& { return line.at(end_side); }
  auto joiningSegmentPoint(const size_t& side, const size_t& point_index) -> Point&
  {
    return pointAt(joiningSegmentPoints(side), point_index);
  }
  auto joiningSegmentPoint(const size_t& side, const size_t& point_index) const -> const Point&
  {
    return pointAt(joiningSegmentPoints(side), point_index);
  }
  auto joiningRegionPoint(const size_t& side, const size_t& point_index) -> Point&
  {
    return pointAt(joiningRegionPoints(side), point_index);
  }
  auto joiningRegionPoint(const size_t& side, const size_t& point_index) const -> const Point&
  {
    return pointAt(joiningRegionPoints(side), point_index);
  }
  static auto checkPointDelay(Point& point) -> void;
  auto checkJoiningSegmentMergeSegment() const -> void;
  auto checkUpdatedJoiningSegment(const Area* current_area, Line& left_line, Line& right_line) const -> void;
  /**
   * @brief data
   *
   */
  size_t _id = 0;

  std::vector<std::unique_ptr<Area>> _owned_areas;
  std::vector<Area*> _unmerged_nodes;
  std::optional<Point> _root_guide;
  TopoType _topo_type = TopoType::kInputTopo;

  Area* _root = nullptr;

  double _skew_bound = 0;
  RCPattern _pattern = RCPattern::kHV;

  double _unit_horizontal_capacitance = 0.0;
  double _unit_horizontal_resistance = 0.0;
  double _unit_vertical_capacitance = 0.0;
  double _unit_vertical_resistance = 0.0;
  AxisDelayFactor _delay_quadratic_factor;

  SideState<Points> _joining_region;
  SideState<Points> _joining_segment;
  SideState<TransformedRect> _merge_segment;
  EndState<Points> _balance_points;  // balance points which skew equal to 0
  EndState<Point> _joining_corner;
  EndState<Points> _feasible_merge_segment_points;  // feasible merging section
};

}  // namespace icts::bst
