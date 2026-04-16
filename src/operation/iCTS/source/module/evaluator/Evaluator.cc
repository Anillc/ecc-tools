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
 * @file Evaluator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "Evaluator.hh"

#include <utility>

#include "context/EvaluatorRuntimeContext.hh"
#include "log/Log.hh"

namespace icts {

Evaluator& Evaluator::getInst()
{
  static Evaluator instance;
  return instance;
}

void Evaluator::reset()
{
  _is_initialized = false;
  _is_evaluated = false;
  _have_calc = false;
  _runtime_context.reset();
  _metrics = {};
  _eval_nets.clear();
}

EvaluatorRuntimeContextInterface& Evaluator::context()
{
  if (_runtime_context == nullptr) {
    _runtime_context = CreateDefaultEvaluatorRuntimeContext();
  }
  return *_runtime_context;
}

const EvaluatorRuntimeContextInterface& Evaluator::context() const
{
  if (_runtime_context == nullptr) {
    _runtime_context = CreateDefaultEvaluatorRuntimeContext();
  }
  return *_runtime_context;
}

void Evaluator::setRuntimeContext(std::shared_ptr<EvaluatorRuntimeContextInterface> runtime_context)
{
  _runtime_context = std::move(runtime_context);
}

void Evaluator::printLog()
{
  LOG_INFO << "\033[1;31m";
  LOG_INFO << R"(                  _             _              )";
  LOG_INFO << R"(                 | |           | |             )";
  LOG_INFO << R"(   _____   ____ _| |_   _  __ _| |_ ___  _ __  )";
  LOG_INFO << R"(  / _ \ \ / / _` | | | | |/ _` | __/ _ \| '__| )";
  LOG_INFO << R"( |  __/\ V / (_| | | |_| | (_| | || (_) | |    )";
  LOG_INFO << R"(  \___| \_/ \__,_|_|\__,_|\__,_|\__\___/|_|    )";
  LOG_INFO << "\033[0m";
  LOG_INFO << "Enter evaluator!";
}

void Evaluator::init()
{
  if (_is_initialized) {
    return;
  }
  _timing_analysis_service.initClockPropagation(context());
  printLog();
  _metrics_collector.initLevel(context());
  _metrics_collector.transferEvalNets(context(), _eval_nets);
  _have_calc = false;
  _is_initialized = true;
  _is_evaluated = false;
}

void Evaluator::calcInfo()
{
  if (_have_calc) {
    return;
  }
  _metrics_collector.calcInfo(context(), _eval_nets, _metrics);
  _have_calc = true;
}

void Evaluator::evaluate()
{
  if (_is_evaluated) {
    return;
  }
  if (!_is_initialized) {
    init();
  }
  _timing_analysis_service.evaluateTiming(context(), _eval_nets);
  _have_calc = false;
  _is_evaluated = true;
}

void Evaluator::statistics(const std::string& save_dir)
{
  if (!_have_calc) {
    calcInfo();
  }
  _statistics_writer.writeStatistics(context(), save_dir, _metrics);
}

void Evaluator::plotPath(const std::string& inst_name, const std::string& file) const
{
  _debug_plot_service.plotPath(context(), _eval_nets, inst_name, file, _default_size);
}

void Evaluator::plotNet(const std::string& net_name, const std::string& file) const
{
  _debug_plot_service.plotNet(context(), net_name, file, _default_size);
}

}  // namespace icts
