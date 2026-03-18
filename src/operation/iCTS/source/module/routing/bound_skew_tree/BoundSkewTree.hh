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
 * @brief Stage-1 bound-skew tree router interface
 */
#pragma once
#include <array>
#include <string>
#include <unordered_map>
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
  BoundSkewTree(std::vector<Area*> load_areas, const BSTParameters& parameters, const TopoType& topo_type);
  BoundSkewTree(Area* root, const BSTParameters& parameters);
  ~BoundSkewTree() = default;

  void run();
  Area* get_root() const { return _root; }

  void set_root_guide(const double& x, const double& y) { _root_guide = Point(x, y, 0, 0, 0); }

  void set_pattern(const RCPattern& pattern) { _pattern = pattern; }

 private:
  /**
   * @brief match
   *
   */
  using CostFunc = std::function<double(Area*, Area*)>;
  Match getBestMatch(CostFunc cost_func) const;
  double mergeCost(Area* left, Area* right) const;
  double distanceCost(Area* left, Area* right) const;
  /**
   * @brief topology
   *
   */
  Area* merge(Area* left, Area* right);
  void areaReset();
  void ptReset(Area* cur);
  void biPartition();
  Area* biPartition(std::vector<Area*>& areas);
  std::pair<std::vector<Area*>, std::vector<Area*>> octagonDivide(std::vector<Area*>& areas) const;
  std::vector<Point> calcOctagon(const std::vector<Area*>& areas) const;
  std::vector<Area*> areaOnOctagonBound(const std::vector<Area*> areas, const std::vector<Point>& octagon) const;
  void biCluster();
  Area* biCluster(const std::vector<Area*>& areas);
  std::vector<std::vector<Area*>> kMeansPlus(const std::vector<Area*>& areas, const size_t& k, const int& seed = 0,
                                             const size_t& max_iter = 10) const;
  /**
   * @brief flow require
   *
   */
  // main flow
  void bottomUp();
  void bottomUpAllPairBased();
  void bottomUpTopoBased();
  void recursiveBottomUp(Area* cur);
  void topDown();

  // main interface
  void merge(Area* parent, Area* left, Area* right);
  void calcJS(Area* parent, Area* left, Area* right);
  void jsProcess(Area* cur);
  void constructMr(Area* parent, Area* left, Area* right);
  void embedding(Area* cur) const;

  // Join Segment
  void initSide();
  void calcJS(Area* cur, Line& left, Line& right);
  void calcJsDelay(Area* left, Area* right);
  void updateJS(Area* cur, Line& left, Line& right, PointPair closest);
  void addJsPts(Area* parent, Area* left, Area* right);
  double delayFromJs(const size_t& js_side, const size_t& side, const size_t& idx, const size_t& timing_type,
                     const Side<double>& delay_from) const;
  // Join Region
  void calcJr(Area* parent, Area* left, Area* right);
  void calcJrEndpoints(Area* cur);
  void calcNotManhattanJrEndpoints(Area* parent, Area* left, Area* right);
  void addTurnPt(const size_t& side, const size_t& idx, const size_t& timing_type, const Side<double>& delay_from);
  void addFmsToJr();

  // Join Corner
  void calcJrCorner(Area* cur);
  bool jrCornerExist(const size_t& end_side) const;

  // Balance Point
  void calcBalancePt(Area* cur);
  void calcBalBetweenPts(Point& p1, Point& p2, const size_t& timing_type, const size_t& bal_ref_side, double& d1, double& d2, Point& bal_pt,
                         const RCPattern& pattern) const;
  void calcBalPtOnLine(Point& p1, Point& p2, const size_t& timing_type, double& d1, double& d2, Point& bal_pt,
                       const RCPattern& pattern) const;
  void calcBalPtNotOnLine(Point& p1, Point& p2, const size_t& timing_type, const size_t& bal_ref_side, double& d1, double& d2,
                          Point& bal_pt, const RCPattern& pattern) const;
  void calcMergeDist(const double& r, const double& c, const double& cap1, const double& delay1, const double& cap2, const double& delay2,
                     const double& dist, double& d1, double& d2) const;
  void calcPtCoordOnLine(const Point& p1, const Point& p2, const double& d1, const double& d2, Point& pt) const;
  double calcXBalPosition(const double& delay1, const double& delay2, const double& cap1, const double& cap2, const double& h,
                          const double& v, const size_t& bal_ref_side) const;
  double calcYBalPosition(const double& delay1, const double& delay2, const double& cap1, const double& cap2, const double& h,
                          const double& v, const size_t& bal_ref_side) const;

  // Feasible Merging Section
  void calcFmsPt(Area* cur);
  bool calcFmsOnLine(Area* cur, Point& pt, const Point& q, const size_t& end_side);
  void calcFmsBetweenPts(const Point& high_skew_pt, const Point& low_skew_pt, Point& fms_pt) const;
  bool existFmsOnJr() const;

  // Merging Region
  void constructFeasibleMr(Area* parent, Area* left, Area* right) const;
  bool jRisLine() const;
  void mrBetweenJs(Area* cur, const size_t& end_side) const;
  void mrOnJs(Area* cur, const size_t& side) const;
  void fmsOfLineExist(Area* cur, const size_t& side, const size_t& idx) const;
  double calcSkewSlope(Area* cur) const;

  void constructInfeasibleMr(Area* parent, Area* left, Area* right) const;
  void calcMinSkewSection(Area* cur) const;
  void calcDetourEdgeLen(Area* cur) const;
  void refineMrDelay(Area* cur) const;

  void constructTransformedRectMr(Area* cur) const;

  // Embedding
  void embedding(Area* parent, Area* child, const size_t& side) const;
  bool isTransformedRectArea(Area* cur) const;
  bool isManhattanArea(Area* cur) const;
  void mrToTransformedRect(const Region& mr, TransformedRect& trr) const;

  // basic function
  LineType calcAreaLineType(Area* cur) const;
  void calcConvexHull(Area* cur) const;
  double calcJrArea(const Line& l1, const Line& l2) const;

  void calcBsLocated(Area* cur, Point& pt, Line& line) const;
  void calcPtDelays(Area* cur, Point& pt, Line& line) const;
  void updatePtDelaysByEndSide(Area* cur, const size_t& end_side, Point& pt) const;
  void calcIrregularPtDelays(Area* cur, Point& pt, Line& line) const;
  double ptDelayIncrease(Point& p1, Point& p2, const double& cap, const RCPattern& pattern) const;
  double ptDelayIncrease(Point& p1, Point& p2, const double& len, const double& cap, const RCPattern& pattern) const;
  double calcDelayIncrease(const double& x, const double& y, const double& cap, const RCPattern& pattern) const;
  double ptSkew(const Point& pt) const;
  Line getJrLine(const size_t& side) const;
  Line getJsLine(const size_t& side) const;
  void setJrLine(const size_t& side, const Line& line);
  void setJsLine(const size_t& side, const Line& line);
  void checkPtDelay(Point& pt) const;
  void checkJsMs() const;
  void checkUpdateJs(const Area* cur, Line& left, Line& right) const;
  /**
   * @brief data
   *
   */
  size_t _id = 0;

  std::vector<Area*> _unmerged_nodes;
  std::optional<Point> _root_guide;
  TopoType _topo_type = TopoType::kInputTopo;

  Area* _root = nullptr;

  double _skew_bound = 0;
  RCPattern _pattern = RCPattern::kHV;

  double _unit_h_cap = 0.0;
  double _unit_h_res = 0.0;
  double _unit_v_cap = 0.0;
  double _unit_v_res = 0.0;
  Side<double> _K = {0.0, 0.0};

  Side<Points> _join_region;
  Side<Points> _join_segment;
  Side<TransformedRect> _ms;
  Side<Points> _bal_points;  // balance points which skew equal to 0
  Side<Point> _join_corner;
  Side<Points> _fms_points;  // feasible merging section
};

}  // namespace icts::bst