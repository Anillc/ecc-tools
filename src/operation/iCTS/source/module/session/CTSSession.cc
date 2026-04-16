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
 * @file CTSSession.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "CTSSession.hh"

#include <memory>

#include "CTSAPI.hh"
#include "CtsConfig.hh"
#include "CtsRuntime.hh"

namespace icts {

namespace {

class SessionRuntimeAdapter : public CtsRuntimeInterface
{
 public:
  explicit SessionRuntimeAdapter(const CTSSession& session) : _session(session) {}

  CtsConfig* getConfig() const override { return _session.state().config; }
  CtsDesign* getDesign() const override { return _session.state().design; }
  CtsDBWrapper* getDbWrapper() const override { return _session.state().db_wrapper; }

  double getClockUnitCap(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const override
  {
    return _session.getClockUnitCap(layer_pattern);
  }
  double getClockUnitRes(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const override
  {
    return _session.getClockUnitRes(layer_pattern);
  }
  double getSinkCap(const std::string& load_pin_full_name) const override { return _session.getSinkCap(load_pin_full_name); }
  bool isClockNet(const std::string& net_name) const override { return _session.isClockNet(net_name); }
  bool isTop(const std::string& net_name) const override { return _session.isTop(net_name); }
  bool isInDie(const Point& point) const override { return _session.isInDie(point); }

  bool cellLibExist(const std::string& cell_master) const override { return _session.cellLibExist(cell_master); }
  CtsCellLib* getCellLib(const std::string& cell_master) const override { return _session.getCellLib(cell_master); }
  std::vector<CtsCellLib*> getAllBufferLibs() const override { return _session.getAllBufferLibs(); }
  double getClockAt(const std::string& pin_name, const std::string& belong_clock_name) const override
  {
    return _session.getClockAT(pin_name, belong_clock_name);
  }
  std::string getCellType(const std::string& cell_master) const override { return _session.getCellType(cell_master); }
  double getCellArea(const std::string& cell_master) const override { return _session.getCellArea(cell_master); }
  double getCellLeakagePower(const std::string& cell_master) const override { return _session.getCellLeakagePower(cell_master); }
  double getCellCap(const std::string& cell_master) const override { return _session.getCellCap(cell_master); }

  int32_t getDbUnit() const override { return _session.getDbUnit(); }
  size_t genId() override { return _session.genId(); }
  void registerSynthesisNet(Net* net) override { _session.registerSynthesisNet(net); }
  Net* findSynthesisNet(const std::string& net_name) const override { return _session.findSynthesisNet(net_name); }
  void clearSynthesisNets() override { _session.clearSynthesisNets(); }

  void setPropagateClock() const override { _session.setPropagateClock(); }
  void convertDbToTimingEngine() const override { _session.convertDbToTimingEngine(); }
  void reportTiming() const override { _session.reportTiming(); }
  void refresh() const override { _session.refresh(); }
  void buildRcTree(const EvalNet& eval_net) const override { _session.buildRCTree(eval_net); }

  void checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix = ".rpt") const override
  {
    _session.checkFile(dir, file_name, suffix);
  }
  void logTitle(const std::string& title) const override { _session.logTitle(title); }
  void saveToLog(const std::string& text) const override { _session.saveToLog(text); }
  void logEnd() const override { _session.logEnd(); }
  void latencySkewLog() const override { _session.latencySkewLog(); }
  void utilizationLog() const override { _session.utilizationLog(); }
  void slackLog() const override { _session.slackLog(); }

  const CTSSession& _session;
};

class SessionEvaluatorRuntimeContext : public EvaluatorRuntimeContextInterface
{
 public:
  explicit SessionEvaluatorRuntimeContext(const CTSSession& session) : _session(session) {}

  CtsDesign* getDesign() const override { return _session.state().design; }
  CtsConfig* getConfig() const override { return _session.state().config; }
  CtsDBWrapper* getDbWrapper() const override { return _session.state().db_wrapper; }

  bool isTop(const std::string& net_name) const override { return _session.isTop(net_name); }
  bool isClockNet(const std::string& net_name) const override { return _session.isClockNet(net_name); }
  bool cellLibExist(const std::string& cell_master) const override { return _session.cellLibExist(cell_master); }
  std::string getCellType(const std::string& cell_master) const override { return _session.getCellType(cell_master); }
  double getCellArea(const std::string& cell_master) const override { return _session.getCellArea(cell_master); }
  double getCellCap(const std::string& cell_master) const override { return _session.getCellCap(cell_master); }
  int getDbUnit() const override { return _session.getDbUnit(); }
  Net* findSynthesisNet(const std::string& net_name) const override { return _session.findSynthesisNet(net_name); }

  void setPropagateClock() const override { _session.setPropagateClock(); }
  void refresh() const override { _session.refresh(); }
  void buildRCTree(const EvalNet& eval_net) const override { _session.buildRCTree(eval_net); }
  void reportTiming() const override { _session.reportTiming(); }

  void checkFile(const std::string& dir, const std::string& file_name) const override { _session.checkFile(dir, file_name); }
  void latencySkewLog() const override { _session.latencySkewLog(); }
  void utilizationLog() const override { _session.utilizationLog(); }
  void slackLog() const override { _session.slackLog(); }

  void logTitle(const std::string& title) const override { _session.logTitle(title); }
  void saveToLog(const std::string& text) const override { _session.saveToLog(text); }
  void saveToLog(const std::string& key, const std::string& value) const override { _session.saveToLog(key, value); }
  void saveToLog(const std::string& key, int value) const override { _session.saveToLog(key, value); }
  void saveToLog(const std::string& key, const char* value) const override { _session.saveToLog(key, value); }
  void logEnd() const override { _session.logEnd(); }

  const CTSSession& _session;
};

}  // namespace

CTSSession::CTSSession(CTSAPI& api) : _api(&api), _state(std::make_shared<CTSState>())
{
}

CTSAPI& CTSSession::api() const
{
  return *_api;
}

CTSState& CTSSession::state()
{
  return *_state;
}

const CTSState& CTSSession::state() const
{
  return *_state;
}

CTSContext CTSSession::buildContext() const
{
  CTSContext context;
  context.config = _state->config;
  context.design = _state->design;
  context.db_wrapper = _state->db_wrapper;
  context.report = _state->report;
  context.log_ofs = _state->log_ofs;
  context.libs = _state->libs;
  context.evaluator = _state->evaluator;
  context.model_factory = _state->model_factory;
  context.timing_engine = _state->timing_engine;
  context.solver_runtime.min_buffering_length_provider
      = [this]() -> double { return _state->config == nullptr ? 0.0 : static_cast<double>(_state->config->get_min_buffering_length()); };
  context.solver_runtime.cell_lib_exist_provider = [this](const std::string& cell_master) { return cellLibExist(cell_master); };
  context.solver_runtime.gen_id_provider = [this]() { return genId(); };
  context.solver_runtime.save_log = [this](const std::string& msg) { saveToLog(msg); };
  context.solver_runtime.cell_area_provider = [this](const std::string& cell_master) { return getCellArea(cell_master); };
  context.solver_runtime.cell_leakage_power_provider = [this](const std::string& cell_master) { return getCellLeakagePower(cell_master); };
  context.committer_runtime._design = _state->design;
  context.committer_runtime._db_wrapper = _state->db_wrapper;
  context.committer_runtime._is_in_die = [this](const Point& point) { return isInDie(point); };
  context.committer_runtime._register_synthesis_net = [this](Net* net) { registerSynthesisNet(net); };
  context.committer_runtime._sync_timing = [this]() { convertDbToTimingEngine(); };
  context.source_runtime = std::make_shared<SessionRuntimeAdapter>(*this);
  context.evaluator_runtime = std::make_shared<SessionEvaluatorRuntimeContext>(*this);
  return context;
}

void CTSSession::installSourceRuntime() const
{
  CtsRuntimeRegistry::install(std::make_shared<SessionRuntimeAdapter>(*this));
}

void CTSSession::clearSourceRuntime() const
{
  CtsRuntimeRegistry::clear();
}

double CTSSession::getClockUnitCap(const std::optional<LayerPattern>& layer_pattern) const
{
  return _state->config == nullptr ? 0.0 : _api->getClockUnitCap(layer_pattern);
}

double CTSSession::getClockUnitRes(const std::optional<LayerPattern>& layer_pattern) const
{
  return _state->config == nullptr ? 0.0 : _api->getClockUnitRes(layer_pattern);
}

double CTSSession::getSinkCap(const std::string& load_pin_full_name) const
{
  return _api->getSinkCap(load_pin_full_name);
}

bool CTSSession::isClockNet(const std::string& net_name) const
{
  return _api->isClockNet(net_name);
}

bool CTSSession::isTop(const std::string& net_name) const
{
  return _api->isTop(net_name);
}

bool CTSSession::isInDie(const Point& point) const
{
  return _api->isInDie(point);
}

bool CTSSession::cellLibExist(const std::string& cell_master) const
{
  return _api->cellLibExist(cell_master);
}

CtsCellLib* CTSSession::getCellLib(const std::string& cell_master) const
{
  return _api->getCellLib(cell_master);
}

std::vector<CtsCellLib*> CTSSession::getAllBufferLibs() const
{
  return _api->getAllBufferLibs();
}

double CTSSession::getClockAT(const std::string& pin_name, const std::string& belong_clock_name) const
{
  return _api->getClockAT(pin_name, belong_clock_name);
}

std::string CTSSession::getCellType(const std::string& cell_master) const
{
  return _api->getCellType(cell_master);
}

double CTSSession::getCellArea(const std::string& cell_master) const
{
  return _api->getCellArea(cell_master);
}

double CTSSession::getCellLeakagePower(const std::string& cell_master) const
{
  return _api->getCellLeakagePower(cell_master);
}

double CTSSession::getCellCap(const std::string& cell_master) const
{
  return _api->getCellCap(cell_master);
}

int32_t CTSSession::getDbUnit() const
{
  return _api->getDbUnit();
}

size_t CTSSession::genId() const
{
  return static_cast<size_t>(_api->genId());
}

void CTSSession::registerSynthesisNet(Net* net) const
{
  _api->registerSynthesisNet(net);
}

Net* CTSSession::findSynthesisNet(const std::string& net_name) const
{
  return _api->findSynthesisNet(net_name);
}

void CTSSession::clearSynthesisNets() const
{
  _api->clearSynthesisNets();
}

void CTSSession::setPropagateClock() const
{
  _api->setPropagateClock();
}

void CTSSession::convertDbToTimingEngine() const
{
  _api->convertDBToTimingEngine();
}

void CTSSession::reportTiming() const
{
  _api->reportTiming();
}

void CTSSession::refresh() const
{
  _api->refresh();
}

void CTSSession::buildRCTree(const EvalNet& eval_net) const
{
  _api->buildRCTree(eval_net);
}

void CTSSession::checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix) const
{
  _api->checkFile(dir, file_name, suffix);
}

void CTSSession::logTitle(const std::string& title) const
{
  _api->logTitle(title);
}

void CTSSession::saveToLog(const std::string& text) const
{
  _api->saveToLog(text);
}

void CTSSession::saveToLog(const std::string& key, const std::string& value) const
{
  _api->saveToLog(key, value);
}

void CTSSession::saveToLog(const std::string& key, int value) const
{
  _api->saveToLog(key, value);
}

void CTSSession::saveToLog(const std::string& key, const char* value) const
{
  _api->saveToLog(key, value);
}

void CTSSession::logEnd() const
{
  _api->logEnd();
}

void CTSSession::latencySkewLog() const
{
  _api->latencySkewLog();
}

void CTSSession::utilizationLog() const
{
  _api->utilizationLog();
}

void CTSSession::slackLog() const
{
  _api->slackLog();
}

}  // namespace icts
