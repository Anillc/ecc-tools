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
 * @file QorEvaluation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree evaluation stage.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "qor/Qor.hh"

namespace icts {

class ClockLayout;

struct QorSummary
{
  struct ClockTiming
  {
    std::string clock_name;
    double setup_tns = 0.0;
    double setup_wns = 0.0;
    double hold_tns = 0.0;
    double hold_wns = 0.0;
    double suggest_freq = 0.0;
  };
  struct ClockLatencySkew
  {
    std::string clock_name;
    std::string analysis_mode;
    std::string launch_pin;
    std::string capture_pin;
    double launch_latency_ns = 0.0;
    double capture_latency_ns = 0.0;
    double worst_skew_ns = 0.0;
    double average_worst_skew_ns = 0.0;
    std::size_t path_count = 0U;
    std::size_t average_sample_count = 0U;
  };

  bool has_evaluation_result = false;
  std::string qor_metric_status = "unavailable";
  std::string timing_metric_source = "unavailable";
  std::string physical_metric_source = "unavailable";
  std::string path_depth_metric_status = "unavailable";
  bool sta_clocks_propagated = false;
  std::size_t propagated_clock_count = 0U;
  int32_t final_clock_buffer_count = 0;
  double final_buffer_area_um2 = 0.0;
  int32_t clock_member_buffer_count = 0;
  double max_clock_net_wirelength_um = 0.0;
  double total_clock_network_wirelength_um = 0.0;
  int32_t max_clock_net_wirelength_dbu = 0;
  double total_clock_network_wirelength_dbu = 0.0;
  int32_t design_dbu_per_um = 0;

  // Compatibility aliases for existing feature consumers. Do not use these
  // names in cts.log unless real path/depth traversal is implemented.
  int32_t buffer_num = 0;
  double buffer_area = 0.0;
  int32_t clock_path_min_buffer = 0;
  int32_t clock_path_max_buffer = 0;
  int32_t feature_max_clock_network_level = 0;
  int32_t max_clock_wirelength = 0;
  double total_clock_wirelength = 0.0;
  std::vector<ClockTiming> clocks_timing;
  std::vector<ClockLatencySkew> clocks_latency_skew;
};

struct EvaluationOptions
{
  bool refresh_sta_timing = false;
  const ClockLayout* clock_layout = nullptr;
};

struct EvaluationState
{
  QorSummary summary;
  Qor statistics;
};

class QorEvaluation
{
 public:
  QorEvaluation() = delete;

  static auto evaluate(EvaluationState& state) -> void;
  static auto evaluate(EvaluationState& state, const EvaluationOptions& options) -> void;
  static auto outputSummary(const EvaluationState& state) -> QorSummary;
  static auto hasEvaluationResult(const EvaluationState& state) -> bool;
  static auto reset(EvaluationState& state) -> void;
};

}  // namespace icts
