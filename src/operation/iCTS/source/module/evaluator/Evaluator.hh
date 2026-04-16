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
 * @file Evaluator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "EvalNet.hh"
#include "EvaluatorData.hh"
#include "context/EvaluatorRuntimeContext.hh"
#include "service/DebugPlotService.hh"
#include "service/MetricsCollector.hh"
#include "service/StatisticsWriter.hh"
#include "service/TimingAnalysisService.hh"

namespace icts {

class Evaluator
{
 public:
  static Evaluator& getInst();

  Evaluator(const Evaluator&) = delete;
  Evaluator(Evaluator&&) = delete;
  Evaluator& operator=(const Evaluator&) = delete;
  Evaluator& operator=(Evaluator&&) = delete;

  void reset();

  void init();
  void evaluate();

  void statistics(const std::string& save_dir);

  void plotPath(const std::string& inst, const std::string& file = "debug.gds") const;
  void plotNet(const std::string& net_name, const std::string& file = "debug.gds") const;

  void calcInfo();
  [[nodiscard]] bool isInitialized() const { return _is_initialized; }
  [[nodiscard]] bool isEvaluated() const { return _is_evaluated; }
  std::map<std::string, int> get_cell_dist() const { return _metrics.cell_dist_map; }
  std::map<std::string, CellStatsProperty> get_cell_stats() const { return _metrics.cell_stats_map; }
  std::vector<PathInfo> get_path_infos() const { return _metrics.path_infos; }
  double get_max_net_len() const { return _metrics.max_net_len; }
  double get_total_wire_len() const { return _metrics.total_wire_len; }
  void setRuntimeContext(std::shared_ptr<EvaluatorRuntimeContextInterface> runtime_context);

 private:
  Evaluator() = default;
  ~Evaluator() = default;

  void printLog();
  EvaluatorRuntimeContextInterface& context();
  const EvaluatorRuntimeContextInterface& context() const;

  bool _is_initialized = false;
  bool _is_evaluated = false;
  bool _have_calc = false;
  mutable std::shared_ptr<EvaluatorRuntimeContextInterface> _runtime_context;
  EvaluatorMetrics _metrics;
  std::vector<EvalNet> _eval_nets;

  TimingAnalysisService _timing_analysis_service;
  MetricsCollector _metrics_collector;
  StatisticsWriter _statistics_writer;
  DebugPlotService _debug_plot_service;
  const int _default_size = 100;
};

}  // namespace icts
