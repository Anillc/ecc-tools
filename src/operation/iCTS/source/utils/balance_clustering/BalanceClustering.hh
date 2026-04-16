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
 * @file BalanceClustering.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once
#include <limits>
#include <vector>

#include "Pin.hh"

namespace icts {
enum class EnhanceType
{
  kMinDelay,
  kMaxDelay,
  kWorstViolation,
};
struct ViolationScore
{
  double skew_vio_score;
  double cap_vio_score;
  std::vector<Pin*> cluster;
};

/**
 * @brief BalanceClustering class
 *       clustering sinks by max distance and max fanout
 *       using k-means algorithm construct a initial clustering
 *       then iteratively adjust the clustering by min cost flow to balance the capacitance variance
 *
 */
class BalanceClustering
{
 public:
  BalanceClustering() = delete;
  ~BalanceClustering() = default;
  static std::vector<std::vector<Pin*>> kMeansPlus(const std::vector<Pin*>& load_pins, const size_t& k, const int& seed = 0,
                                                   const size_t& max_iter = 100, const size_t& no_change_stop = 5);

  static std::vector<std::vector<Pin*>> kMeans(const std::vector<Pin*>& load_pins, const size_t& k, const int& seed = 0,
                                               const size_t& max_iter = 100);

  static std::vector<std::vector<Pin*>> iterClustering(const std::vector<Pin*>& load_pins, const size_t& max_fanout,
                                                       const size_t& iters = 100, const size_t& no_change_stop = 5,
                                                       const double& limit_ratio = 0.8, const bool& log = false);

  static std::vector<std::vector<Pin*>> slackClustering(const std::vector<std::vector<Pin*>>& clusters, const double& max_net_length,
                                                        const size_t& max_fanout);

  static std::vector<std::vector<Pin*>> clusteringEnhancement(const std::vector<std::vector<Pin*>>& clusters, const int& max_fanout,
                                                              const double& max_cap, const double& max_net_length, const double& skew_bound,
                                                              const size_t& max_iter = 200, const double& cooling_rate = 0.99,
                                                              const double& temperature = 50000);

  static std::vector<Pin*> getMinDelayCluster(const std::vector<std::vector<Pin*>>& clusters);

  static std::vector<Pin*> getMaxDelayCluster(const std::vector<std::vector<Pin*>>& clusters);

  static std::vector<Pin*> getWorstViolationCluster(const std::vector<std::vector<Pin*>>& clusters);

  static std::vector<std::vector<Pin*>> getMostRecentClusters(const std::vector<std::vector<Pin*>>& clusters,
                                                              const std::vector<Pin*>& center_cluster, const size_t& num_limit = 42,
                                                              const size_t& cluster_num_limit = 4);

  static std::vector<std::vector<Pin*>> balancedBiPartition(const std::vector<Pin*>& load_pins, const double& tolerance = 0.1,
                                                            const size_t& seed_trials = 8, const bool& log = false);

  static std::vector<Point> getCentroids(const std::vector<std::vector<Pin*>>& clusters);

  static std::pair<std::vector<Pin*>, std::vector<Pin*>> divideBy(const std::vector<Pin*>& load_pins,
                                                                  const std::function<double(Pin*)>& func, const double& ratio);

  static std::pair<std::vector<std::vector<Pin*>>, std::vector<std::vector<Pin*>>> getBoundCluster(
      const std::vector<std::vector<Pin*>>& clusters);

  static void latencyOpt(const std::vector<Pin*>& cluster, const double& skew_bound, const double& ratio);

  static double estimateSkew(const std::vector<Pin*>& cluster);

  static double estimateNetDelay(const std::vector<Pin*>& cluster, const bool& is_max = true);

  static double estimateNetCap(const std::vector<Pin*>& cluster);

  static double estimateNetLength(const std::vector<Pin*>& cluster);

  static Point calcCentroid(const std::vector<Pin*>& cluster);

  static Point calcBoundCentroid(const std::vector<Pin*>& cluster);

  static int calcHPMD(const std::vector<Pin*>& cluster);

  static double calcHPWL(const std::vector<Pin*>& cluster);

  static double calcSAE(const std::vector<std::vector<Pin*>>& clusters, const std::vector<Point>& buffers);

  static double calcBalanceVariance(const std::vector<std::vector<Pin*>>& clusters, const std::vector<Point>& buffers,
                                    const double& cap_coef = 1.0, const double& delay_coef = 1.0);

  static double calcCapVariance(const std::vector<std::vector<Pin*>>& clusters, const std::vector<Point>& buffers);

  static double calcDelayVariance(const std::vector<std::vector<Pin*>>& clusters, const std::vector<Point>& buffers);

  static double calcVariance(const std::vector<double>& values);

  static ViolationScore calcScore(const std::vector<Pin*>& cluster);

  static double crossProduct(const Point& p1, const Point& p2, const Point& p3);

  static void convexHull(std::vector<Point>& pts);

  static std::vector<CtsPoint<double>> paretoFront(const std::vector<CtsPoint<double>>& pts);

  static bool isContain(const Point& p, const std::vector<Point>& pts);

  static bool isSame(const std::vector<std::vector<Pin*>>& clusters1, const std::vector<std::vector<Pin*>>& clusters2);

  static void writeClusterPy(const std::vector<std::vector<Pin*>>& clusters, const std::string& save_name = "clusters");
};
}  // namespace icts
