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
 * @file CTSAPI.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "CTSAPI.hh"

#include <chrono>
#include <filesystem>
#include <functional>
#include <ranges>
#include <unordered_map>

#include "CBS.hh"
#include "CTSFlow.hh"
#include "CTSSession.hh"
#include "CtsCellLib.hh"
#include "CtsConfig.hh"
#include "CtsDBWrapper.hh"
#include "CtsDesign.hh"
#include "Evaluator.hh"
#include "GDSPloter.hh"
#include "JsonParser.hh"
#include "ModelFactory.hh"
#include "Node.hh"
#include "Pin.hh"
#include "Router.hh"
#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "builder.h"
#include "feature_icts.h"
#include "feature_ista.h"
#include "idm.h"
#include "log/Log.hh"
#include "report/CtsReport.hh"
#include "sta/StaBuildClockTree.hh"
#include "usage/usage.hh"
namespace icts {
#define DBCONFIG (dmInst->get_config())

CTSAPI* CTSAPI::_cts_api_instance = nullptr;

CTSAPI& CTSAPI::getInst()
{
  if (_cts_api_instance == nullptr) {
    _cts_api_instance = new CTSAPI();
  }
  return *_cts_api_instance;
}
void CTSAPI::destroyInst()
{
  if (_cts_api_instance != nullptr) {
    delete _cts_api_instance;
    _cts_api_instance = nullptr;
  }
}

CTSSession& CTSAPI::requireSession() const
{
  LOG_FATAL_IF(_session == nullptr) << "CTS session is not initialized.";
  return *_session;
}

CTSState& CTSAPI::mutableState()
{
  return requireSession().state();
}

const CTSState& CTSAPI::sessionState() const
{
  return requireSession().state();
}

std::ofstream* CTSAPI::logStream() const
{
  return _session == nullptr ? nullptr : sessionState().log_ofs;
}
// open API

void CTSAPI::runCTS()
{
  CTSFlowRunner::run(*this);
}

void CTSAPI::writeDB()
{
  auto& state = mutableState();
  state.db_wrapper->writeDef(state.config->get_output_def_path());
}

void CTSAPI::writeGDS()
{
  const auto* config = _session == nullptr ? nullptr : sessionState().config;
  const auto design_gds_path = config == nullptr ? std::string() : config->get_gds_file();
  std::string flyline_gds_path;
  if (!design_gds_path.empty()) {
    flyline_gds_path = std::filesystem::path(design_gds_path).parent_path().append("cts_flyline.gds").string();
  }
  GDSPloter::plotDesign(design_gds_path);
  GDSPloter::plotFlyLine(flyline_gds_path);
  GDSPloter::writePyDesign();
  GDSPloter::writeJsonDesign();
  GDSPloter::writePyFlyLine();
}

void CTSAPI::report(const std::string& save_dir)
{
  CTSFlowRunner::report(*this, save_dir);
}

void CTSAPI::initEvalInfo()
{
  CTSFlowRunner::initEvalInfo(*this);
}

size_t CTSAPI::getInsertCellNum() const
{
  auto cell_dist_map = sessionState().evaluator->get_cell_dist();
  size_t insert_num = 0;
  for (auto& [cell_master, cell_dist] : cell_dist_map) {
    insert_num += cell_dist;
  }
  return insert_num;
}

double CTSAPI::getInsertCellArea() const
{
  auto cell_stats = sessionState().evaluator->get_cell_stats();
  double insert_area = 0.0;
  for (auto& [cell_master, cell_stat] : cell_stats) {
    insert_area += cell_stat.total_area;
  }
  return insert_area;
}

std::vector<PathInfo> CTSAPI::getPathInfos() const
{
  return sessionState().evaluator->get_path_infos();
}

double CTSAPI::getMaxClockNetWL() const
{
  return sessionState().evaluator->get_max_net_len();
}

double CTSAPI::getTotalClockNetWL() const
{
  return sessionState().evaluator->get_total_wire_len();
}

void CTSAPI::resetAPI()
{
  if (_session != nullptr) {
    auto& state = _session->state();
    if (state.timing_engine != nullptr) {
      state.timing_engine->destroyTimingEngine();
      state.timing_engine = nullptr;
    }
    _session->clearSourceRuntime();
    state.clear();
  }
  _session.reset();
}

void CTSAPI::init(const std::string& config_file, const std::string& work_dir)
{
  CTSFlowRunner::init(*this, config_file, work_dir);
}

icts::CtsConfig* CTSAPI::getConfig() const
{
  return _session == nullptr ? nullptr : sessionState().config;
}

// iSTA
void CTSAPI::dumpVertexData(const std::vector<std::string>& vertex_names) const
{
  sessionState().timing_engine->get_ista()->dumpVertexData(vertex_names);
}

double CTSAPI::getClockUnitCap(const std::optional<icts::LayerPattern>& layer_pattern) const
{
  const auto& state = sessionState();
  const auto pattern = layer_pattern.value_or(icts::LayerPattern::kNone);
  auto* db_adapter = getStaDbAdapter();
  const auto default_layer = state.config->get_routing_layers().back();
  const auto layer = pattern == icts::LayerPattern::kH   ? state.config->get_h_layer()
                     : pattern == icts::LayerPattern::kV ? state.config->get_v_layer()
                                                         : default_layer;
  LOG_ERROR_IF(pattern != icts::LayerPattern::kH && pattern != icts::LayerPattern::kV && pattern != icts::LayerPattern::kNone)
      << "Unknown layer pattern";
  std::optional<double> width = std::nullopt;
  auto max_len = state.config->get_max_length();
  return db_adapter->getCapacitance(layer, max_len, width) / max_len;
}

double CTSAPI::getClockUnitRes(const std::optional<icts::LayerPattern>& layer_pattern) const
{
  const auto& state = sessionState();
  const auto pattern = layer_pattern.value_or(icts::LayerPattern::kNone);
  auto* db_adapter = getStaDbAdapter();
  const auto default_layer = state.config->get_routing_layers().back();
  const auto layer = pattern == icts::LayerPattern::kH   ? state.config->get_h_layer()
                     : pattern == icts::LayerPattern::kV ? state.config->get_v_layer()
                                                         : default_layer;
  LOG_ERROR_IF(pattern != icts::LayerPattern::kH && pattern != icts::LayerPattern::kV && pattern != icts::LayerPattern::kNone)
      << "Unknown layer pattern";
  std::optional<double> width = std::nullopt;
  auto max_len = state.config->get_max_length();
  return db_adapter->getResistance(layer, max_len, width) / max_len / 2;
}

double CTSAPI::getSinkCap(icts::CtsInstance* sink) const
{
  auto* load_pin = sink->get_load_pin();
  return getSinkCap(load_pin->get_full_name());
}

double CTSAPI::getSinkCap(const std::string& load_pin_full_name) const
{
  // remove all "\" in inst_name
  auto name = load_pin_full_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  return sessionState().timing_engine->getInstPinCapacitance(name.c_str());
}

bool CTSAPI::isFlipFlop(const std::string& inst_name) const
{
  // remove all "\" in inst_name
  auto name = inst_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  return sessionState().timing_engine->isSequentialCell(name.c_str());
}

bool CTSAPI::isClockNet(const std::string& net_name) const
{
  return findStaNet(net_name)->isClockNet();
}

void CTSAPI::startDbSta()
{
  auto& state = mutableState();
  state.timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(state.timing_engine->get_ista());
  db_adapter->set_idb(state.db_wrapper->get_idb());
  state.timing_engine->set_db_adapter(std::move(db_adapter));
  readSTAFile();
  state.timing_engine->get_ista()->set_n_worst_path_per_clock(10);
}

void CTSAPI::readClockNetNames() const
{
  const auto& state = sessionState();
  state.timing_engine->updateTiming();
  auto* netlist = state.timing_engine->get_netlist();
  auto idb = state.db_wrapper->get_idb();
  auto idb_design = idb->get_def_service()->get_design();
  auto* idb_net_list = idb_design->get_net_list();
  ista::Net* sta_net = nullptr;
  FOREACH_NET(netlist, sta_net)
  {
    if (sta_net->isClockNet()) {
      auto* sta_clock = state.timing_engine->getPropClockOfNet(sta_net);
      auto idb_net = idb_net_list->find_net(sta_net->get_name());
      if (idb_net->has_io_pins()) {
        int io_pin_num = idb_net->get_io_pins()->get_pin_num();
        int inst_pin_num = idb_net->get_instance_pin_list()->get_pin_num();
        if (io_pin_num == 1 && inst_pin_num <= 1) {
          LOG_WARNING << "Clock :" << sta_clock->get_clock_name() << ", Net: " << sta_net->get_name()
                      << " has no valid io pin coordinates, skipping.";
          idb_net->set_connect_type(IdbConnectType::kClock);
          continue;  // skip nets with no valid io pin coordinates
        }
      }
      state.design->addClockNetName(sta_clock->get_clock_name(), sta_net->get_name());
      LOG_INFO << "Clock [" << sta_clock->get_clock_name() << "] have net \"" << sta_net->get_name() << "\"";
      CTSAPI_INST.saveToLog("Clock [", sta_clock->get_clock_name(), "] have net \"", sta_net->get_name(), "\"");
    }
  }
}

void CTSAPI::setPropagateClock()
{
  auto* ista = sessionState().timing_engine->get_ista();
  auto& the_constrain = ista->get_constrains();
  auto& the_sdc_clocks = the_constrain->get_sdc_clocks();
  for (auto& the_sdc_clock : the_sdc_clocks) {
    the_sdc_clock.second->set_is_propagated();
  }
}

void CTSAPI::convertDBToTimingEngine()
{
  auto& state = mutableState();
  state.timing_engine->resetNetlist();
  state.timing_engine->resetGraph();
  state.timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  const char* sdc_path = DBCONFIG.get_sdc_path().c_str();
  state.timing_engine->readSdc(sdc_path);
  state.timing_engine->buildGraph();
}

void CTSAPI::reportTiming() const
{
  ieda::Stats stats;
  sessionState().timing_engine->updateTiming();
  CTSAPI_INST.saveToLog("iSTA update timing elapsed time: ", stats.elapsedRunTime(), "s");
  sessionState().timing_engine->reportTiming({}, true, true);
}

void CTSAPI::refresh()
{
  // _timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  sessionState().timing_engine->updateTiming();
}

icts::CtsPin* CTSAPI::findDriverPin(icts::CtsNet* net)
{
  if (net == nullptr) {
    return nullptr;
  }
  auto* sta_net = findStaNet(net->get_net_name());
  if (sta_net == nullptr || sta_net->getDriver() == nullptr) {
    return net->get_driver_pin();
  }
  auto driver_pin_name = sta_net->getDriver()->get_name();
  auto* driver_pin = net->findPin(driver_pin_name);
  return driver_pin != nullptr ? driver_pin : net->get_driver_pin();
}

std::map<std::string, double> CTSAPI::elmoreDelay(const icts::EvalNet& eval_net)
{
  auto* db_adapter = getStaDbAdapter();
  auto* sta_net = db_adapter->createNet(eval_net.get_name().c_str(), nullptr);
  buildRCTree(eval_net);
  auto* rc_net = sessionState().timing_engine->get_ista()->getRcNet(sta_net);
  auto* rc_tree = rc_net->rct();
  std::map<std::string, double> delay_map;
  for (auto pin : eval_net.get_pins()) {
    if (pin == eval_net.get_driver_pin()) {
      continue;
    }
    auto pin_name = pin->get_full_name();
    auto delay = rc_tree->delay(pin_name);
    const auto pin_key = pin->get_instance() == nullptr ? pin_name : pin->get_instance()->get_name();
    delay_map[pin_key] = delay;
  }
  db_adapter->deleteNet(sta_net);
  return delay_map;
}

bool CTSAPI::cellLibExist(const std::string& cell_master, const std::string& query_field, const std::string& from_port,
                          const std::string& to_port)
{
  if (cell_master.empty()) {
    return false;
  }
  auto* timing_engine = sessionState().timing_engine;
  if (timing_engine->findLibertyCell(cell_master.c_str()) == nullptr) {
    return false;
  }

  std::vector<std::vector<double>> index_list;
  ista::LibTable::TableType table_type;
  if (query_field == "cell_rise") {
    table_type = ista::LibTable::TableType::kCellRise;
  } else if (query_field == "cell_fall") {
    table_type = ista::LibTable::TableType::kCellFall;
  } else if (query_field == "rise_transition") {
    table_type = ista::LibTable::TableType::kRiseTransition;
  } else if (query_field == "fall_transition") {
    table_type = ista::LibTable::TableType::kFallTransition;
  } else {
    LOG_FATAL << "buffer lib query field not supported";
  }
  ista::LibTable* table = nullptr;
  if (from_port.empty() && to_port.empty()) {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), table_type);
  } else {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), from_port.c_str(), to_port.c_str(), table_type);
  }
  return table != nullptr;
}

std::vector<std::vector<double>> CTSAPI::queryCellLibIndex(const std::string& cell_master, const std::string& query_field,
                                                           const std::string& from_port, const std::string& to_port)
{
  auto* timing_engine = sessionState().timing_engine;
  std::vector<std::vector<double>> index_list;
  ista::LibTable::TableType table_type;
  if (query_field == "cell_rise") {
    table_type = ista::LibTable::TableType::kCellRise;
  } else if (query_field == "cell_fall") {
    table_type = ista::LibTable::TableType::kCellFall;
  } else if (query_field == "rise_transition") {
    table_type = ista::LibTable::TableType::kRiseTransition;
  } else if (query_field == "fall_transition") {
    table_type = ista::LibTable::TableType::kFallTransition;
  } else {
    LOG_FATAL << "buffer lib query field not supported";
  }
  ista::LibTable* table = nullptr;
  if (from_port.empty() && to_port.empty()) {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), table_type);
  } else {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), from_port.c_str(), to_port.c_str(), table_type);
  }
  auto& axes = table->get_axes();
  for (auto& axis : axes) {
    auto& axis_values = axis.get()->get_axis_values();
    std::vector<double> index;
    for (auto& axis_value : axis_values) {
      index.push_back(axis_value.get()->getFloatValue());
    }
    index_list.push_back(index);
  }
  return index_list;
}

std::vector<double> CTSAPI::queryCellLibValue(const std::string& cell_master, const std::string& query_field, const std::string& from_port,
                                              const std::string& to_port)
{
  auto* timing_engine = sessionState().timing_engine;
  std::vector<double> values;
  ista::LibTable::TableType table_type;
  if (query_field == "cell_rise") {
    table_type = ista::LibTable::TableType::kCellRise;
  } else if (query_field == "cell_fall") {
    table_type = ista::LibTable::TableType::kCellFall;
  } else if (query_field == "rise_transition") {
    table_type = ista::LibTable::TableType::kRiseTransition;
  } else if (query_field == "fall_transition") {
    table_type = ista::LibTable::TableType::kFallTransition;
  } else {
    LOG_FATAL << "buffer lib query field not supported";
  }
  ista::LibTable* table = nullptr;
  if (from_port.empty() && to_port.empty()) {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), table_type);
  } else {
    table = timing_engine->getCellLibertyTable(cell_master.c_str(), from_port.c_str(), to_port.c_str(), table_type);
  }
  auto& table_values = table->get_table_values();

  for (auto& table_value : table_values) {
    values.push_back(table_value.get()->getFloatValue());
  }
  return values;
}

icts::CtsCellLib* CTSAPI::getCellLib(const std::string& cell_master, const std::string& from_port, const std::string& to_port,
                                     const bool& use_work_value)
{
  auto& state = mutableState();
  CtsCellLib* lib = state.libs->findLib(cell_master);
  if (lib) {
    return lib;
  }
  auto index_list = queryCellLibIndex(cell_master, "cell_rise", from_port, to_port);
  std::vector<double> rise_delay = queryCellLibValue(cell_master, "cell_rise", from_port, to_port);
  std::vector<double> fall_delay = queryCellLibValue(cell_master, "cell_fall", from_port, to_port);
  std::vector<double> rise_slew = queryCellLibValue(cell_master, "rise_transition", from_port, to_port);
  std::vector<double> fall_slew = queryCellLibValue(cell_master, "fall_transition", from_port, to_port);

  auto calc_mid_value = [](const std::vector<double>& rise_values, const std::vector<double>& fall_values) {
    std::vector<double> mid_values;
    for (size_t i = 0; i < rise_values.size(); ++i) {
      mid_values.emplace_back((rise_values[i] + fall_values[i]) / 2);
    }
    return mid_values;
  };

  auto delay_mid_value = calc_mid_value(rise_delay, fall_delay);
  auto slew_mid_value = calc_mid_value(rise_slew, fall_slew);

  lib = new CtsCellLib(cell_master, index_list, delay_mid_value, slew_mid_value);
  // set init cap by liberty
  auto init_cap = getCellCap(cell_master);
  lib->set_init_cap(init_cap);
  lib->set_area(getCellArea(cell_master));
  lib->set_leakage_power(getCellLeakagePower(cell_master));
  // fit linear coef
  auto slew_in = index_list[0];
  auto cap_out = index_list[1];

  std::vector<double> x_slew_in;
  std::vector<double> x_cap_out;
  std::vector<double> y_delay;
  std::vector<double> y_slew;

  for (size_t i = 0; i < slew_in.size(); ++i) {
    auto work_slew = slew_in[i];
    if (work_slew > state.config->get_max_buf_tran() && use_work_value) {
      break;
    }
    for (size_t j = 0; j < cap_out.size(); ++j) {
      auto work_cap = cap_out[j];
      if (work_cap > state.config->get_max_cap() && use_work_value) {
        break;
      }
      x_slew_in.emplace_back(work_slew);
      x_cap_out.emplace_back(work_cap);
      y_delay.emplace_back(delay_mid_value[i * cap_out.size() + j]);
      y_slew.emplace_back(slew_mid_value[i * cap_out.size() + j]);
    }
  }
  if (x_slew_in.empty() || x_cap_out.empty() || y_delay.empty() || y_slew.empty()) {
    LOG_WARNING << "No feasible work value, please check "
                   "the config parameter: \"max_buf_tran\", \"max_sink_tran\" and "
                   "\"max_cap\" with the liberty "
                << cell_master;
    // use all value
    x_slew_in = slew_in;
    x_cap_out = cap_out;
    y_delay = delay_mid_value;
    y_slew = slew_mid_value;
  }
  std::vector<std::vector<double>> x_delay = {x_slew_in, x_cap_out};
  lib->set_delay_coef(state.model_factory->cppLinearModel(x_delay, y_delay));

  std::vector<std::vector<double>> x_slew = {x_cap_out};
  lib->set_slew_coef(state.model_factory->cppLinearModel(x_slew, y_slew));

  state.libs->insertLib(cell_master, lib);
  return lib;
}

std::vector<icts::CtsCellLib*> CTSAPI::getAllBufferLibs()
{
  auto buffer_types = sessionState().config->get_buffer_types();
  std::vector<icts::CtsCellLib*> all_buf_libs;
  std::ranges::for_each(buffer_types, [&](const std::string& buf_cell) {
    auto* buf_lib = getCellLib(buf_cell);
    all_buf_libs.emplace_back(buf_lib);
  });
  auto cmp = [](CtsCellLib* lib_1, CtsCellLib* lib_2) {
    if (lib_1->get_area() != lib_2->get_area()) {
      return lib_1->get_area() < lib_2->get_area();
    }
    if (lib_1->get_init_cap() != lib_2->get_init_cap()) {
      return lib_1->get_init_cap() < lib_2->get_init_cap();
    }
    return lib_1->getDelayIntercept() < lib_2->getDelayIntercept();
  };
  std::ranges::sort(all_buf_libs, cmp);
  return all_buf_libs;
}

std::vector<std::string> CTSAPI::getMasterClocks(icts::CtsNet* net) const
{
  auto* sta_net = findStaNet(net->get_net_name());
  return sessionState().timing_engine->getMasterClocksOfNet(sta_net);
}

double CTSAPI::getClockAT(const std::string& pin_name, const std::string& belong_clock_name) const
{
  auto clk_at
      = sessionState().timing_engine->getClockAT(pin_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise, belong_clock_name);
  if (clk_at == std::nullopt) {
    LOG_WARNING << "get " << pin_name << " clock arrival time failed, which belong clock " << belong_clock_name;
    return 0.0;
  }
  return clk_at.value();
}

std::string CTSAPI::getCellType(const std::string& cell_master) const
{
  return sessionState().timing_engine->getCellType(cell_master.c_str());
}

double CTSAPI::getCellArea(const std::string& cell_master) const
{
  return sessionState().timing_engine->getCellArea(cell_master.c_str());
}

double CTSAPI::getCellLeakagePower(const std::string& cell_master) const
{
  if (cell_master.empty()) {
    return 0.0;
  }
  auto* liberty_cell = sessionState().timing_engine->findLibertyCell(cell_master.c_str());
  if (liberty_cell == nullptr) {
    return 0.0;
  }
  return liberty_cell->get_cell_leakage_power();
}

double CTSAPI::getCellCap(const std::string& cell_master) const
{
  auto* timing_engine = sessionState().timing_engine;
  if (cell_master.empty() || timing_engine->findLibertyCell(cell_master.c_str()) == nullptr) {
    return 0.0;
  }
  auto input_pin_names = timing_engine->getLibertyCellInputpin(cell_master.c_str());
  if (input_pin_names.empty()) {
    return 0.0;
  }
  auto cell_pin_name = CTSAPI_INST.toString(cell_master.c_str(), ":", input_pin_names[0].c_str());
  auto init_cap = timing_engine->getLibertyCellPinCapacitance(cell_pin_name.c_str());
  return init_cap;
}

double CTSAPI::getSlewIn(const std::string& pin_name) const
{
  return sessionState().timing_engine->getSlew(pin_name.c_str(), ista::AnalysisMode::kMin, ista::TransType::kRise);
}

double CTSAPI::getCapOut(const std::string& pin_name) const
{
  return sessionState().timing_engine->getInstPinCapacitance(pin_name.c_str(), ista::AnalysisMode::kMin, ista::TransType::kRise);
}

std::vector<double> CTSAPI::solvePolynomialRealRoots(const std::vector<double>& coeffs)
{
  return mutableState().model_factory->solvePolynomialRealRoots(coeffs);
}

// synthesis
int32_t CTSAPI::getDbUnit() const
{
  auto* idb = dmInst->get_idb_builder();
  auto* idb_design = idb->get_def_service()->get_design();
  return idb_design->get_units()->get_micron_dbu();
}

bool CTSAPI::isInDie(const icts::Point& point) const
{
  auto* die = sessionState().db_wrapper->get_core_bounding_box();
  auto pt = sessionState().db_wrapper->ctsToIdb(point);
  return die->containPoint(pt);
}

idb::IdbInstance* CTSAPI::makeIdbInstance(const std::string& inst_name, const std::string& cell_master)
{
  auto* db_adapter = getStaDbAdapter();
  auto sta_inst = db_adapter->createInstance(sessionState().timing_engine->findLibertyCell(cell_master.c_str()), inst_name.c_str());
  auto idb_inst = db_adapter->staToDb(sta_inst);
  return idb_inst;
}

idb::IdbNet* CTSAPI::makeIdbNet(const std::string& net_name)
{
  auto* db_adapter = getStaDbAdapter();
  auto sta_net = db_adapter->createNet(net_name.c_str(), nullptr);
  auto idb_net = db_adapter->staToDb(sta_net);
  return idb_net;
}

void CTSAPI::linkIdbNetToSta(idb::IdbNet* idb_net)
{
  auto* db_adapter = getStaDbAdapter();
  auto sta_net = db_adapter->createNet(idb_net->get_net_name().c_str(), nullptr);
  db_adapter->crossRef(sta_net, idb_net);
}

void CTSAPI::disconnect(idb::IdbPin* pin)
{
  auto* db_adapter = getStaDbAdapter();
  auto sta_pin = db_adapter->dbToStaPin(pin);
  db_adapter->disattachPin(sta_pin);
}

void CTSAPI::connect(idb::IdbInstance* idb_inst, const std::string& pin_name, idb::IdbNet* net)
{
  auto* db_adapter = getStaDbAdapter();
  auto sta_inst = sessionState().timing_engine->get_netlist()->findInstance(idb_inst->get_name().c_str());
  auto sta_net = db_adapter->dbToSta(net);
  db_adapter->attach(sta_inst, pin_name.c_str(), sta_net);
}

void CTSAPI::insertBuffer(const std::string& name)
{
  sessionState().timing_engine->insertBuffer(name.c_str());
}

void CTSAPI::resetId()
{
  mutableState().design->resetId();
}

int CTSAPI::genId()
{
  return mutableState().design->nextId();
}

void CTSAPI::registerSynthesisNet(Net* net)
{
  if (net == nullptr) {
    return;
  }
  mutableState().synthesis_net_map[net->get_name()] = net;
}

Net* CTSAPI::findSynthesisNet(const std::string& net_name) const
{
  const auto& synthesis_net_map = sessionState().synthesis_net_map;
  const auto it = synthesis_net_map.find(net_name);
  return it == synthesis_net_map.end() ? nullptr : it->second;
}

void CTSAPI::clearSynthesisNets()
{
  mutableState().synthesis_net_map.clear();
}

void CTSAPI::genFluteTree(const std::string& net_name, icts::Pin* driver, const std::vector<icts::Pin*>& loads)
{
  TreeBuilder::fluteTree(net_name, driver, loads);
}

void CTSAPI::genShallowLightTree(const std::string& net_name, icts::Pin* driver, const std::vector<icts::Pin*>& loads)
{
  TreeBuilder::shallowLightTree(net_name, driver, loads);
}

icts::Inst* CTSAPI::genBoundSkewTree(const std::string& net_name, const std::vector<icts::Pin*>& loads,
                                     const std::optional<double>& skew_bound, const std::optional<icts::Point>& guide_loc,
                                     const TopoType& topo_type)
{
  return TreeBuilder::boundSkewTree(net_name, loads, skew_bound, guide_loc, topo_type);
}

icts::Inst* CTSAPI::genBstSaltTree(const std::string& net_name, const std::vector<icts::Pin*>& loads,
                                   const std::optional<double>& skew_bound, const std::optional<icts::Point>& guide_loc,
                                   const TopoType& topo_type)
{
  return TreeBuilder::bstSaltTree(net_name, loads, skew_bound, guide_loc, topo_type);
}

icts::Inst* CTSAPI::genCBSTree(const std::string& net_name, const std::vector<icts::Pin*>& loads, const std::optional<double>& skew_bound,
                               const std::optional<icts::Point>& guide_loc, const TopoType& topo_type)
{
  return TreeBuilder::cbsTree(net_name, loads, skew_bound, guide_loc, topo_type);
}

// evaluate
bool CTSAPI::isTop(const std::string& net_name) const
{
  return sessionState().design->isClockTopNet(net_name);
}

void CTSAPI::buildRCTree(const std::vector<icts::EvalNet>& eval_nets)
{
  for (auto& eval_net : eval_nets) {
    buildRCTree(eval_net);
  }
}

void CTSAPI::buildRCTree(const icts::EvalNet& eval_net)
{
  auto& state = mutableState();
  auto net_name = eval_net.get_name();
#ifdef DEBUG_ICTS_EVALUATOR
  LOG_INFO << "Evaluate: " << net_name;
#endif
  resetRCTree(net_name);
  auto* sta_net = findStaNet(eval_net);
  auto layer_id = state.config->get_routing_layers().back();
  auto* solver_net = findSynthesisNet(net_name);
  if (!solver_net) {
    LOG_WARNING << "Can't find solver net: " << net_name << ", It may be a pin-port(s) net";
    return;
  }
  auto* driver_pin = solver_net->get_driver_pin();
  if (driver_pin == nullptr) {
    LOG_WARNING << "Can't find solver driver pin: " << net_name;
    return;
  }
  driver_pin->preOrder([&](Node* node) {
    auto* parent = node->get_parent();
    if (parent == nullptr) {
      return;
    }
    auto parent_name = parent->get_name();  // pin_full_name or node's 'steriner_{id}' name
    auto child_name = node->get_name();
    ista::RctNode* front_node = makeRCTreeNode(eval_net, parent_name);
    ista::RctNode* back_node = makeRCTreeNode(eval_net, child_name);
    double len = TimingPropagator::calcLen(parent, node);
    auto res = getResistance(len, layer_id);
    auto cap = getCapacitance(len, layer_id);
    state.timing_engine->makeResistor(sta_net, front_node, back_node, res);
    state.timing_engine->incrCap(front_node, cap / 2, true);
    state.timing_engine->incrCap(back_node, cap / 2, true);
  });

  state.timing_engine->updateRCTreeInfo(sta_net);
}

void CTSAPI::buildPinPortsRCTree(const icts::EvalNet& eval_net)
{
  auto& state = mutableState();
  auto* sta_net = findStaNet(eval_net);
  auto net_name = eval_net.get_name();
  LOG_FATAL_IF(!sta_net) << "Can't find sta net: " << net_name;
  auto pins = sta_net->get_pin_ports();
  ista::DesignObject* driver_pin = nullptr;
  for (auto* pin : pins) {
    if (pin->isPin()) {
      driver_pin = pin;
      break;
    }
  }
  LOG_FATAL_IF(!driver_pin) << "Can't find driver pin of sta net: " << net_name;
  auto* driver_node = state.timing_engine->makeOrFindRCTreeNode(driver_pin);
  auto* db_adapter = getStaDbAdapter();
  auto driver_loc = db_adapter->idbLocation(driver_pin);
  auto pt_dist = [](idb::IdbCoordinate<int32_t>* p1, idb::IdbCoordinate<int32_t>* p2) {
    return std::abs(p1->get_x() - p2->get_x()) + std::abs(p1->get_y() - p2->get_y());
  };
  std::ranges::for_each(pins, [&](ista::DesignObject* pin) {
    if (pin == driver_pin) {
      return;
    }
    auto* load_node = state.timing_engine->makeOrFindRCTreeNode(pin);
    auto load_loc = db_adapter->idbLocation(pin);
    auto dist = pt_dist(driver_loc, load_loc);
    auto res = getResistance(1.0 * dist / TimingPropagator::getDbUnit(), state.config->get_routing_layers().back());
    auto cap = getCapacitance(1.0 * dist / TimingPropagator::getDbUnit(), state.config->get_routing_layers().back());
    state.timing_engine->makeResistor(sta_net, driver_node, load_node, res);
    state.timing_engine->incrCap(driver_node, cap / 2, true);
    state.timing_engine->incrCap(load_node, cap / 2, true);
  });
  state.timing_engine->updateRCTreeInfo(sta_net);
}

void CTSAPI::resetRCTree(const std::string& net_name)
{
  auto* sta_net = findStaNet(net_name);
  sessionState().timing_engine->resetRcTree(sta_net);
}

void CTSAPI::utilizationLog() const
{
  CTSAPI_INST.logTitle("Summary of Utilization");
  auto* idb_design = dmInst->get_idb_design();
  auto* idb_layout = dmInst->get_idb_layout();
  int dbu = idb_design->get_units()->get_micron_dbu() < 0 ? idb_layout->get_units()->get_micron_dbu()
                                                          : idb_design->get_units()->get_micron_dbu();
  auto* idb_die = idb_layout->get_die();
  auto die_width = ((double) idb_die->get_width()) / dbu;
  auto die_height = ((double) idb_die->get_height()) / dbu;

  auto idb_core_box = idb_layout->get_core()->get_bounding_box();
  auto core_width = ((double) idb_core_box->get_width()) / dbu;
  auto core_height = ((double) idb_core_box->get_height()) / dbu;
  CTSAPI_INST.mirrorToTerminalAndLog("DIE Area ( um^2 ): ",
                                     ieda::Str::printf("%f = %03f * %03f", die_width * die_height, die_width, die_height));
  CTSAPI_INST.mirrorToTerminalAndLog("DIE Usage: ", dmInst->dieUtilization() * 100, "%");
  CTSAPI_INST.mirrorToTerminalAndLog("CORE Area ( um^2 ): ",
                                     ieda::Str::printf("%f = %03f * %03f", core_width * core_height, core_width, core_height));
  CTSAPI_INST.mirrorToTerminalAndLog("CORE Usage: ", dmInst->coreUtilization() * 100, "%");
  CTSAPI_INST.logEnd();
}

void CTSAPI::latencySkewLog() const
{
  CTSAPI_INST.logTitle("Summary of Latency & Skew");

  auto fix_point_str = [](double data) { return std::string(ieda::Str::printf("%.3f", data)); };
  std::vector<std::pair<std::string, ista::AnalysisMode>> mode_list
      = {{"Setup", ista::AnalysisMode::kMax}, {"Hold", ista::AnalysisMode::kMin}};
  for (const auto& [clk, seq_path_group] : sessionState().timing_engine->get_ista()->get_clock_groups()) {
    CTSAPI_INST.mirrorToTerminalAndLog("Clock: ", clk->get_clock_name());
    for (auto& [mode_str, mode] : mode_list) {
      auto cmp_mode = mode;
      auto cmp = [&](ista::StaPathData* left, ista::StaPathData* right) -> bool {
        int left_skew = left->getSkew();
        int right_skew = right->getSkew();
        return cmp_mode == ista::AnalysisMode::kMax ? (left_skew > right_skew) : (left_skew < right_skew);
      };
      CTSAPI_INST.mirrorToTerminalAndLog("\t[", mode_str, " Mode]");
      std::priority_queue<ista::StaPathData*, std::vector<ista::StaPathData*>, decltype(cmp)> seq_data_queue(cmp);

      ista::StaPathEnd* path_end;
      ista::StaPathData* path_data;
      FOREACH_PATH_GROUP_END(seq_path_group.get(), path_end)
      FOREACH_PATH_END_DATA(path_end, mode, path_data)
      {
        seq_data_queue.push(path_data);
      }
      auto* worst_seq_data = seq_data_queue.top();
      auto* launch_clock_data = worst_seq_data->get_launch_clock_data();
      auto* capture_clock_data = worst_seq_data->get_capture_clock_data();

      auto* launch_clock_vertex = launch_clock_data->get_own_vertex();
      auto* capture_clock_vertex = capture_clock_data->get_own_vertex();

      CTSAPI_INST.mirrorToTerminalAndLog("\t\tLaunch Latency: ", fix_point_str(FS_TO_NS(launch_clock_data->get_arrive_time())), " From ",
                                         launch_clock_vertex->getNameWithCellName());
      CTSAPI_INST.mirrorToTerminalAndLog("\t\tCapture Latency: ", fix_point_str(FS_TO_NS(capture_clock_data->get_arrive_time())), " From ",
                                         capture_clock_vertex->getNameWithCellName());
      CTSAPI_INST.mirrorToTerminalAndLog("\t\tMax Skew: ", fix_point_str(FS_TO_NS(worst_seq_data->getSkew())));
      // calc avg skew
      int total_skew = 0;
      unsigned n_worst = 10;
      unsigned i = 0;
      while (!seq_data_queue.empty() && i < n_worst) {
        auto* seq_path_data = dynamic_cast<ista::StaSeqPathData*>(seq_data_queue.top());
        total_skew += seq_path_data->getSkew();
        seq_data_queue.pop();
        i++;
      }
      auto total_skew_ns = FS_TO_NS(total_skew);
      auto avg_skew = total_skew_ns / (double) n_worst;
      CTSAPI_INST.mirrorToTerminalAndLog("\t\tAvg Skew: ", avg_skew, " (worst 10)");
    }
  }
  CTSAPI_INST.logEnd();
}

void CTSAPI::slackLog() const
{
  auto fix_point_str = [](double data) { return std::string(ieda::Str::printf("%.3f", data)); };
  CTSAPI_INST.logTitle("Summary of WNS & TNS");
  auto* timing_engine = sessionState().timing_engine;
  auto clk_list = timing_engine->getClockList();
  std::ranges::for_each(clk_list, [&](ista::StaClock* clk) {
    auto clk_name = clk->get_clock_name();
    auto setup_tns = timing_engine->getTNS(clk_name, AnalysisMode::kMax);
    auto setup_wns = timing_engine->getWNS(clk_name, AnalysisMode::kMax);
    auto hold_tns = timing_engine->getTNS(clk_name, AnalysisMode::kMin);
    auto hold_wns = timing_engine->getWNS(clk_name, AnalysisMode::kMin);
    auto suggest_freq = 1000.0 / (clk->getPeriodNs() - setup_wns);
    CTSAPI_INST.mirrorToTerminalAndLog("Clk name: ", clk_name);
    CTSAPI_INST.mirrorToTerminalAndLog("\tSetup (Max) WNS: ", fix_point_str(setup_wns), " (ns)");
    CTSAPI_INST.mirrorToTerminalAndLog("\tSetup (Max) TNS: ", fix_point_str(setup_tns), " (ns)");
    CTSAPI_INST.mirrorToTerminalAndLog("\tHold (Min) WNS: ", fix_point_str(hold_wns), " (ns)");
    CTSAPI_INST.mirrorToTerminalAndLog("\tHold (Min) TNS: ", fix_point_str(hold_tns), " (ns)");
    CTSAPI_INST.mirrorToTerminalAndLog("\tSuggest Freq: ", fix_point_str(suggest_freq), " (MHz)");
  });
  CTSAPI_INST.logEnd();
}

// log
void CTSAPI::checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix) const
{
  std::string now_time = ieda::Time::getNowWallTime();
  std::string tmp = Str::replace(now_time, ":", "_");
  std::string origin_file_name = Str::printf("%s/%s%s", dir.c_str(), file_name.c_str(), suffix.c_str());
  std::string copy_design_work_space = Str::printf("%s/%s_%s_%s%s", dir.c_str(), file_name.c_str(), tmp.c_str(), "_backup", suffix.c_str());

  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
    return;
  }
  if (std::filesystem::exists(origin_file_name)) {
    std::filesystem::copy_file(origin_file_name, copy_design_work_space);
  }
}

void CTSAPI::mirrorToTerminalAndLog(const std::string& text)
{
  LOG_INFO << text;
  saveToLog(text);
}

void CTSAPI::logTime() const
{
  std::string time_str = ieda::Time::getNowWallTime();
  std::string str = ieda::Str::printf("Generate the iCTS log at %s", time_str.c_str());
  LOG_INFO << str;
  CTSAPI_INST.saveToLog(str);
  CTSAPI_INST.saveToLog("");
}

void CTSAPI::logLine() const
{
  constexpr std::string_view k_line = "-------------------------------------------------------------------";
  LOG_INFO << k_line;
  CTSAPI_INST.saveToLog(k_line);
}

void CTSAPI::logTitle(const std::string& title) const
{
  std::string time_str = ieda::Time::getNowWallTime();
  std::string str = ieda::Str::printf("[%s] -- Start Time : %s", title.c_str(), time_str.c_str());
  CTSAPI_INST.logLine();
  LOG_INFO << str;
  CTSAPI_INST.saveToLog(str);
  CTSAPI_INST.logLine();
}

void CTSAPI::logEnd() const
{
  CTSAPI_INST.logLine();
  CTSAPI_INST.saveToLog("\n");
}

// function
std::vector<std::string> CTSAPI::splitString(std::string str, const char split)
{
  std::vector<std::string> string_list;

  std::istringstream iss(str);
  std::string token;
  while (getline(iss, token, split)) {
    string_list.push_back(token);
  }
  return string_list;
}

void CTSAPI::writeVerilog() const
{
  sessionState().timing_engine->writeVerilog("cts_debug.v");
}

void CTSAPI::toPyArray(const icts::Point& point, const std::string& label)
{
  CTSAPI_INST.saveToLog(label, "=[[", point.x(), ",", point.y(), "]]");
}

// private STA
void CTSAPI::readSTAFile()
{
  auto& state = mutableState();
  auto* timing_engine = state.timing_engine;
  auto sta_work_dir = std::filesystem::path(state.config->get_work_dir()).append("sta").string();
  if (!std::filesystem::exists(sta_work_dir)) {
    std::filesystem::create_directories(sta_work_dir);
  }
  std::vector<const char*> lib_paths;
  std::ranges::for_each(DBCONFIG.get_lib_paths(), [&](const std::string& lib_path) { lib_paths.push_back(lib_path.c_str()); });
  timing_engine->set_num_threads(80);
  timing_engine->set_design_work_space(sta_work_dir.c_str());
  std::set<std::string> cell_set;
  std::ranges::for_each(state.config->get_buffer_types(), [&](const std::string& buf_cell) { cell_set.insert(buf_cell); });
  timing_engine->get_ista()->addLinkCells(cell_set);
  timing_engine->readLiberty(lib_paths);
  convertDBToTimingEngine();

  timing_engine->initRcTree();
}

ista::RctNode* CTSAPI::makeRCTreeNode(const icts::EvalNet& eval_net, const std::string& name)
{
  auto* sta_net = findStaNet(eval_net);
  auto* cts_pin = sessionState().design->findPin(name);
  if (cts_pin == nullptr) {
    std::vector<std::string> string_list = splitString(name, '_');
    if (string_list.size() == 2 && (string_list[0] == "steiner")) {
      return sessionState().timing_engine->makeOrFindRCTreeNode(sta_net, std::stoi(string_list[1]));
    } else {
      LOG_FATAL << "Unknown pin name: " << name;
    }
  }
  return makePinRCTreeNode(cts_pin);
}

ista::RctNode* CTSAPI::makePinRCTreeNode(icts::CtsPin* pin)
{
  auto* pin_port = findStaPin(pin->is_io() ? pin->get_pin_name() : pin->get_full_name());
  return sessionState().timing_engine->makeOrFindRCTreeNode(pin_port);
}

ista::DesignObject* CTSAPI::findStaPin(icts::CtsPin* pin) const
{
  return findStaPin(pin->get_full_name());
}

ista::DesignObject* CTSAPI::findStaPin(const std::string& pin_full_name) const
{
  // remove all "\" in inst_name
  auto name = pin_full_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  return sessionState().timing_engine->get_netlist()->findObj(name.c_str(), false, false).front();
}

ista::Net* CTSAPI::findStaNet(const icts::EvalNet& eval_net) const
{
  return findStaNet(eval_net.get_clk_net()->get_net_name());
}

ista::Net* CTSAPI::findStaNet(const std::string& name) const
{
  return sessionState().timing_engine->get_netlist()->findNet(name.c_str());
}

double CTSAPI::getCapacitance(const double& wire_length, const int& level) const
{
  std::optional<double> width = std::nullopt;
  return getStaDbAdapter()->getCapacitance(level, wire_length, width);
}

double CTSAPI::getResistance(const double& wire_length, const int& level) const
{
  std::optional<double> width = std::nullopt;
  return getStaDbAdapter()->getResistance(level, wire_length, width) / 2;
}

ista::TimingIDBAdapter* CTSAPI::getStaDbAdapter() const
{
  return dynamic_cast<ista::TimingIDBAdapter*>(sessionState().timing_engine->get_db_adapter());
}

ieda_feature::CTSSummary CTSAPI::outputSummary()
{
  return CTSFlowRunner::outputSummary(*this);
}

}  // namespace icts
