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
 * @file CTSFlow.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "CTSFlow.hh"

#include <algorithm>
#include <filesystem>
#include <memory>

#include "CTSAPI.hh"
#include "CTSSession.hh"
#include "CtsCellLib.hh"
#include "CtsConfig.hh"
#include "CtsDBWrapper.hh"
#include "CtsDesign.hh"
#include "Evaluator.hh"
#include "JsonParser.hh"
#include "ModelFactory.hh"
#include "Router.hh"
#include "TimingPropagator.hh"
#include "api/TimingEngine.hh"
#include "feature_icts.h"
#include "idm.h"
#include "report/CtsReport.hh"
#include "usage/usage.hh"

namespace icts {

void CTSFlowRunner::readData(CTSAPI& api, const CTSContext& context)
{
  api.logTitle("Read Data");
  ieda::Stats stats;
  if (context.config->is_use_netlist()) {
    auto& net_list = context.config->get_clock_netlist();
    for (auto& net : net_list) {
      context.design->addClockNetName(net.first, net.second);
    }
  } else {
    api.readClockNetNames();
  }

  context.db_wrapper->read(context.design);
  api.saveToLog("**Read Data memory usage ", stats.memoryDelta(), "MB");
  api.saveToLog("**Read Data elapsed time ", stats.elapsedRunTime(), "s");
  api.logEnd();
}

void CTSFlowRunner::routing(CTSAPI& api, const CTSContext& context)
{
  api.logTitle("Start CTS Routing");
  ieda::Stats stats;
  Router::getInst().run(context);
  LOG_INFO << "**Routing memory usage " << stats.memoryDelta() << "MB";
  LOG_INFO << "**Routing elapsed time " << stats.elapsedRunTime() << "s";
  api.saveToLog("**Routing memory usage ", stats.memoryDelta(), "MB");
  api.saveToLog("**Routing elapsed time ", stats.elapsedRunTime(), "s");
  api.logEnd();
}

void CTSFlowRunner::evaluate(CTSAPI& api, const CTSContext& context)
{
  api.logTitle("Start CTS Evaluate");
  ieda::Stats stats;
  if (context.timing_engine == nullptr) {
    api.startDbSta();
  }

  context.evaluator->setRuntimeContext(context.evaluator_runtime);
  context.evaluator->init();
  context.evaluator->evaluate();

  LOG_INFO << "**Evaluate memory usage " << stats.memoryDelta() << "MB";
  LOG_INFO << "**Evaluate elapsed time " << stats.elapsedRunTime() << "s";
  api.saveToLog("**Evaluate memory usage ", stats.memoryDelta(), "MB");
  api.saveToLog("**Evaluate elapsed time ", stats.elapsedRunTime(), "s");
  api.logEnd();
}

void CTSFlowRunner::init(CTSAPI& api, const std::string& config_file, const std::string& work_dir)
{
  resetSession(api);
  api._session = std::make_shared<CTSSession>(api);
  auto& session = api.requireSession();
  auto& state = session.state();
  state.config = new CtsConfig();
  JsonParser::getInstance().parse(config_file, state.config);
  applyWorkDirOverride(api, work_dir);
  ensureOutputDirs(api);

  state.design = new CtsDesign();
  if (dmInst->get_idb_builder()) {
    state.db_wrapper = new CtsDBWrapper(dmInst->get_idb_builder());
    state.db_wrapper->setRuntimeContext(state.design, state.config->get_output_def_path(),
                                        [&](const std::string& inst_name) { return api.isFlipFlop(inst_name); });
  } else {
    LOG_FATAL << "idb builder is null";
  }

  state.report = new CtsReportTable("iCTS");
  state.log_ofs = new std::ofstream(state.config->get_log_file(), std::ios::out | std::ios::trunc);
  if (!state.log_ofs->is_open()) {
    LOG_WARNING << "Failed to open CTS log file: " << state.config->get_log_file();
  }
  for (const auto& warning : state.config->get_deprecated_config_warnings()) {
    api.saveToLog("[WARNING] ", warning);
  }

  state.libs = new CtsLibs();
  state.evaluator = &Evaluator::getInst();
  state.evaluator->reset();
  state.model_factory = new ModelFactory();
  session.installSourceRuntime();
  state.evaluator->setRuntimeContext(session.buildContext().evaluator_runtime);
  api.startDbSta();
  TimingPropagator::init();
}

void CTSFlowRunner::run(CTSAPI& api)
{
  ieda::Stats stats;
  api.logTime();
  auto context = api.requireSession().buildContext();
  LOG_FATAL_IF(!context.isReady()) << "CTS session context is incomplete.";
  CTSFlowRunner::readData(api, context);
  CTSFlowRunner::routing(api, context);
  CTSFlowRunner::evaluate(api, api.requireSession().buildContext());
  api.writeGDS();
  LOG_INFO << "**Flow memory usage " << stats.memoryDelta() << "MB";
  LOG_INFO << "**Flow elapsed time " << stats.elapsedRunTime() << "s";

  api.logTitle("Summary of iCTS Flow");
  api.saveToLog("**Flow memory usage: ", stats.memoryDelta(), "MB");
  api.saveToLog("**Flow elapsed time: ", stats.elapsedRunTime(), "s");
  api.logEnd();
}

void CTSFlowRunner::report(CTSAPI& api, const std::string& save_dir)
{
  auto& session = api.requireSession();
  auto& state = session.state();
  LOG_FATAL_IF(state.config == nullptr) << "CTS report requires a valid CTS session config. Run run_cts before cts_report.";
  const auto report_dir = save_dir.empty() ? state.config->get_work_dir() : save_dir;
  LOG_FATAL_IF(report_dir.empty()) << "CTS report output directory is empty.";

  if (!report_dir.empty() && !std::filesystem::exists(report_dir)) {
    std::filesystem::create_directories(report_dir);
  }

  api.logTitle("Start CTS Report");
  ieda::Stats stats;

  const bool needs_timing_engine = state.timing_engine == nullptr;
  const bool reran_evaluator = ensureEvaluatorReady(api, true);
  const std::string report_mode = reran_evaluator ? "Report mode: rebuild evaluator timing because no reusable evaluation state exists."
                                                  : "Report mode: reuse evaluator timing from the current CTS session.";
  LOG_INFO << report_mode;
  api.saveToLog(report_mode);
  state.evaluator->statistics(report_dir);
  LOG_INFO << "**Report memory usage " << stats.memoryDelta() << "MB";
  LOG_INFO << "**Report elapsed time " << stats.elapsedRunTime() << "s";
  api.saveToLog("**Report memory usage ", stats.memoryDelta(), "MB");
  api.saveToLog("**Report elapsed time ", stats.elapsedRunTime(), "s");
  api.logEnd();

  if (needs_timing_engine) {
    state.timing_engine->destroyTimingEngine();
    state.timing_engine = nullptr;
  }
}

void CTSFlowRunner::initEvalInfo(CTSAPI& api)
{
  ensureEvaluatorReady(api, true);
  api.requireSession().state().evaluator->calcInfo();
}

ieda_feature::CTSSummary CTSFlowRunner::outputSummary(CTSAPI& api)
{
  ieda_feature::CTSSummary summary;
  const bool needs_timing_engine = api._session == nullptr || api._session->state().timing_engine == nullptr;

  initEvalInfo(api);

  summary.buffer_num = api.getInsertCellNum();
  summary.buffer_area = api.getInsertCellArea();

  auto path_info = api.getPathInfos();
  int max_path = 0;
  int min_path = 0;
  if (!path_info.empty()) {
    max_path = path_info.front().max_depth;
    min_path = path_info.front().min_depth;
    for (auto path : path_info) {
      max_path = std::max(max_path, path.max_depth);
      min_path = std::min(min_path, path.min_depth);
    }
  } else {
    LOG_WARNING << "CTS summary path information is empty.";
  }
  summary.clock_path_min_buffer = min_path;
  summary.clock_path_max_buffer = max_path;
  summary.max_level_of_clock_tree = max_path;
  summary.max_clock_wirelength = api.getMaxClockNetWL();
  summary.total_clock_wirelength = api.getTotalClockNetWL();

  auto* timing_engine = api.requireSession().state().timing_engine;
  auto clk_list = timing_engine->getClockList();
  for (auto* clk : clk_list) {
    ieda_feature::ClockTiming clock_timing;
    auto clk_name = clk->get_clock_name();
    clock_timing.clock_name = clk_name;
    clock_timing.setup_tns = timing_engine->getTNS(clk_name, ista::AnalysisMode::kMax);
    clock_timing.setup_wns = timing_engine->getWNS(clk_name, ista::AnalysisMode::kMax);
    clock_timing.hold_tns = timing_engine->getTNS(clk_name, ista::AnalysisMode::kMin);
    clock_timing.hold_wns = timing_engine->getWNS(clk_name, ista::AnalysisMode::kMin);
    clock_timing.suggest_freq = 1000.0 / (clk->getPeriodNs() - clock_timing.setup_wns);
    summary.clocks_timing.push_back(clock_timing);
  }

  if (needs_timing_engine) {
    timing_engine->destroyTimingEngine();
    api.requireSession().state().timing_engine = nullptr;
  }

  return summary;
}

void CTSFlowRunner::resetSession(CTSAPI& api)
{
  if (api._session != nullptr) {
    auto& state = api._session->state();
    if (state.timing_engine != nullptr) {
      state.timing_engine->destroyTimingEngine();
      state.timing_engine = nullptr;
    }
    api._session->clearSourceRuntime();
    state.clear();
  }
  Router::getInst().reset();
  Evaluator::getInst().reset();
  api._session.reset();
}

void CTSFlowRunner::applyWorkDirOverride(CTSAPI& api, const std::string& work_dir)
{
  if (work_dir.empty()) {
    return;
  }

  auto& state = api.requireSession().state();
  state.config->set_work_dir(work_dir);
  auto log_file_path = std::filesystem::path(work_dir).append("cts.log");
  auto def_path = std::filesystem::path(work_dir).append("output");
  auto gds_file_path = def_path / "cts_design.gds";
  state.config->set_log_file(log_file_path.string());
  state.config->set_gds_file(gds_file_path.string());
  state.config->set_output_def_path(def_path.string());
}

void CTSFlowRunner::ensureOutputDirs(const CTSAPI& api)
{
  const auto& state = api.requireSession().state();
  const auto log_parent = std::filesystem::path(state.config->get_log_file()).parent_path();
  const auto gds_parent = std::filesystem::path(state.config->get_gds_file()).parent_path();
  const auto def_parent = std::filesystem::path(state.config->get_output_def_path());
  if (!std::filesystem::exists(state.config->get_work_dir())) {
    std::filesystem::create_directories(state.config->get_work_dir());
  }
  if (!log_parent.empty() && !std::filesystem::exists(log_parent)) {
    std::filesystem::create_directories(log_parent);
  }
  if (!gds_parent.empty() && !std::filesystem::exists(gds_parent)) {
    std::filesystem::create_directories(gds_parent);
  }
  if (!def_parent.empty() && !std::filesystem::exists(def_parent)) {
    std::filesystem::create_directories(def_parent);
  }
}

void CTSFlowRunner::ensureTimingEngine(CTSAPI& api)
{
  if (api._session == nullptr || api._session->state().timing_engine == nullptr) {
    api.startDbSta();
  }
}

bool CTSFlowRunner::ensureEvaluatorReady(CTSAPI& api, bool run_evaluate)
{
  LOG_FATAL_IF(api._session == nullptr) << "CTS session is not initialized. Run run_cts before requesting CTS evaluation data.";

  auto& session = api.requireSession();
  auto& state = session.state();
  LOG_FATAL_IF(run_evaluate && (state.config == nullptr || state.design == nullptr || state.db_wrapper == nullptr))
      << "CTS evaluation requires initialized config/design/db wrapper state.";
  if (state.evaluator == nullptr) {
    state.evaluator = &Evaluator::getInst();
    state.evaluator->reset();
  }
  bool recreated_timing_engine = false;
  if (run_evaluate) {
    if (state.timing_engine == nullptr) {
      api.startDbSta();
      state.evaluator->reset();
      recreated_timing_engine = true;
    }
  }
  state.evaluator->setRuntimeContext(session.buildContext().evaluator_runtime);
  if (!run_evaluate) {
    return false;
  }

  const bool was_evaluated = state.evaluator->isEvaluated();
  state.evaluator->init();
  state.evaluator->evaluate();
  return recreated_timing_engine || !was_evaluated;
}

}  // namespace icts
