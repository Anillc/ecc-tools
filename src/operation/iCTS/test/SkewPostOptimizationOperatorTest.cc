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
 * @file SkewPostOptimizationOperatorTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CtsCellLib.hh"
#include "CtsConfig.hh"
#include "CtsRuntime.hh"
#include "SkewPostOptimizationOperator.hh"
#include "SolverPipelineData.hh"
#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "gtest/gtest.h"

namespace {

class SkewOptTestRuntime final : public icts::CtsRuntimeInterface
{
 public:
  SkewOptTestRuntime()
  {
    _config.set_max_cap(2.0);
    _config.set_max_fanout(8);
    _config.set_max_buf_tran(10.0);
    _config.set_max_sink_tran(10.0);
    _config.set_skew_bound(0.08);
    _config.set_work_dir("/tmp");

    addBufferLib("bufx1", 0.01, 0.50, 1.0, 0.01);
    addBufferLib("bufx2", 0.03, 0.03, 2.0, 0.02);
  }

  void setSkewBound(double skew_bound) { _config.set_skew_bound(skew_bound); }
  void setMaxCap(double max_cap) { _config.set_max_cap(max_cap); }
  void setMaxFanout(int max_fanout) { _config.set_max_fanout(max_fanout); }
  void setMaxBufTran(double max_buf_tran) { _config.set_max_buf_tran(max_buf_tran); }
  void setMaxSinkTran(double max_sink_tran) { _config.set_max_sink_tran(max_sink_tran); }

  icts::CtsConfig* getConfig() const override { return const_cast<icts::CtsConfig*>(&_config); }
  icts::CtsDesign* getDesign() const override { return nullptr; }
  icts::CtsDBWrapper* getDbWrapper() const override { return nullptr; }

  double getClockUnitCap(const std::optional<icts::LayerPattern>& layer_pattern = std::nullopt) const override
  {
    (void) layer_pattern;
    return 0.001;
  }
  double getClockUnitRes(const std::optional<icts::LayerPattern>& layer_pattern = std::nullopt) const override
  {
    (void) layer_pattern;
    return 1000.0;
  }
  double getSinkCap(const std::string& load_pin_full_name) const override
  {
    (void) load_pin_full_name;
    return 0.002;
  }
  bool isClockNet(const std::string& net_name) const override
  {
    (void) net_name;
    return true;
  }
  bool isTop(const std::string& net_name) const override
  {
    (void) net_name;
    return true;
  }
  bool isInDie(const icts::Point& point) const override
  {
    (void) point;
    return true;
  }

  bool cellLibExist(const std::string& cell_master) const override { return _cell_libs.contains(cell_master); }
  icts::CtsCellLib* getCellLib(const std::string& cell_master) const override
  {
    auto it = _cell_libs.find(cell_master);
    return it == _cell_libs.end() ? nullptr : it->second;
  }
  std::vector<icts::CtsCellLib*> getAllBufferLibs() const override { return _buffer_libs; }
  double getClockAt(const std::string& pin_name, const std::string& belong_clock_name) const override
  {
    (void) pin_name;
    (void) belong_clock_name;
    return 0.0;
  }
  std::string getCellType(const std::string& cell_master) const override
  {
    (void) cell_master;
    return "BUF";
  }
  double getCellArea(const std::string& cell_master) const override
  {
    auto* lib = getCellLib(cell_master);
    return lib == nullptr ? 0.0 : lib->get_area();
  }
  double getCellLeakagePower(const std::string& cell_master) const override
  {
    auto* lib = getCellLib(cell_master);
    return lib == nullptr ? 0.0 : lib->get_leakage_power();
  }
  double getCellCap(const std::string& cell_master) const override
  {
    auto* lib = getCellLib(cell_master);
    return lib == nullptr ? 0.0 : lib->get_init_cap();
  }

  int32_t getDbUnit() const override { return 1000; }
  size_t genId() override { return ++_next_id; }
  void registerSynthesisNet(icts::Net* net) override
  {
    if (net != nullptr) {
      _synthesis_nets[net->get_name()] = net;
    }
  }
  icts::Net* findSynthesisNet(const std::string& net_name) const override
  {
    auto it = _synthesis_nets.find(net_name);
    return it == _synthesis_nets.end() ? nullptr : it->second;
  }
  void clearSynthesisNets() override { _synthesis_nets.clear(); }

  void setPropagateClock() const override {}
  void convertDbToTimingEngine() const override {}
  void reportTiming() const override {}
  void refresh() const override {}
  void buildRcTree(const icts::EvalNet& eval_net) const override { (void) eval_net; }

  void checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix = ".rpt") const override
  {
    (void) dir;
    (void) file_name;
    (void) suffix;
  }
  void logTitle(const std::string& title) const override { (void) title; }
  void saveToLog(const std::string& text) const override { (void) text; }
  void logEnd() const override {}
  void latencySkewLog() const override {}
  void utilizationLog() const override {}
  void slackLog() const override {}

 private:
  void addBufferLib(const std::string& name, double init_cap, double delay, double area, double leakage_power)
  {
    std::vector<std::vector<double>> index_list = {{0.0, 1.0}, {0.0, 1.0}};
    std::vector<double> delay_values = {delay, delay, delay, delay};
    std::vector<double> slew_values = {0.01, 0.01, 0.01, 0.01};
    auto lib = std::make_unique<icts::CtsCellLib>(name, index_list, delay_values, slew_values);
    lib->set_delay_coef({delay, 0.0, 0.0});
    lib->set_slew_coef({0.01, 0.0});
    lib->set_init_cap(init_cap);
    lib->set_area(area);
    lib->set_leakage_power(leakage_power);
    _cell_libs.emplace(name, lib.get());
    _buffer_libs.push_back(lib.get());
    _owned_libs.push_back(std::move(lib));
  }

  icts::CtsConfig _config;
  std::vector<std::unique_ptr<icts::CtsCellLib>> _owned_libs;
  std::unordered_map<std::string, icts::CtsCellLib*> _cell_libs;
  std::vector<icts::CtsCellLib*> _buffer_libs;
  std::unordered_map<std::string, icts::Net*> _synthesis_nets;
  size_t _next_id = 0;
};

class RuntimeGuard
{
 public:
  RuntimeGuard()
  {
    runtime = std::make_shared<SkewOptTestRuntime>();
    icts::CtsRuntimeRegistry::install(runtime);
  }

  ~RuntimeGuard() { icts::CtsRuntimeRegistry::clear(); }

  void initTiming() const { icts::TimingPropagator::init(); }

  std::shared_ptr<SkewOptTestRuntime> runtime;
};

icts::SolverRuntimeContext buildSolverRuntimeContext()
{
  icts::SolverRuntimeContext runtime;
  runtime.min_buffering_length_provider = []() { return 1.0; };
  runtime.cell_lib_exist_provider = [](const std::string& cell_master) { return icts::GetCtsRuntime().cellLibExist(cell_master); };
  runtime.gen_id_provider = []() { return icts::GetCtsRuntime().genId(); };
  runtime.save_log = [](const std::string& text) { (void) text; };
  runtime.cell_area_provider = [](const std::string& cell_master) { return icts::GetCtsRuntime().getCellArea(cell_master); };
  runtime.cell_leakage_power_provider
      = [](const std::string& cell_master) { return icts::GetCtsRuntime().getCellLeakagePower(cell_master); };
  return runtime;
}

struct SimpleTree
{
  icts::SolverPipelineState state;
  icts::Inst* source = nullptr;
  icts::Inst* fast_buf = nullptr;
  icts::Inst* slow_buf = nullptr;
  icts::Inst* fast_sink = nullptr;
  icts::Inst* slow_sink = nullptr;
};

struct PlateauTree
{
  icts::SolverPipelineState state;
  icts::Inst* source = nullptr;
  icts::Inst* fast_buf_a = nullptr;
  icts::Inst* fast_buf_b = nullptr;
  icts::Inst* slow_buf = nullptr;
  icts::Inst* fast_sink_a = nullptr;
  icts::Inst* fast_sink_b = nullptr;
  icts::Inst* slow_sink = nullptr;
};

struct BranchGeometry
{
  int32_t fast_buf_x = 10;
  int32_t slow_buf_x = 70;
  int32_t fast_sink_x = 20;
  int32_t slow_sink_x = 160;
};

SimpleTree buildTwoBranchTree(const std::string& fast_master = "bufx1", const std::string& slow_master = "bufx1",
                              const BranchGeometry& geometry = {})
{
  SimpleTree tree;
  tree.state.net_name = "clk";

  tree.source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  tree.fast_buf = new icts::Inst("fast_buf", icts::Point(geometry.fast_buf_x, 0), icts::InstType::kBuffer);
  tree.slow_buf = new icts::Inst("slow_buf", icts::Point(geometry.slow_buf_x, 0), icts::InstType::kBuffer);
  tree.fast_sink = new icts::Inst("fast_sink", icts::Point(geometry.fast_sink_x, 0), icts::InstType::kSink);
  tree.slow_sink = new icts::Inst("slow_sink", icts::Point(geometry.slow_sink_x, 0), icts::InstType::kSink);

  tree.source->set_cell_master("bufx1");
  tree.fast_buf->set_cell_master(fast_master);
  tree.slow_buf->set_cell_master(slow_master);

  auto* source_driver = tree.source->get_driver_pin();
  auto* fast_load = tree.fast_buf->get_load_pin();
  auto* slow_load = tree.slow_buf->get_load_pin();
  auto* fast_driver = tree.fast_buf->get_driver_pin();
  auto* slow_driver = tree.slow_buf->get_driver_pin();
  auto* fast_sink_load = tree.fast_sink->get_load_pin();
  auto* slow_sink_load = tree.slow_sink->get_load_pin();

  icts::TreeBuilder::directConnectTree(source_driver, fast_load);
  icts::TreeBuilder::directConnectTree(source_driver, slow_load);
  icts::TreeBuilder::directConnectTree(fast_driver, fast_sink_load);
  icts::TreeBuilder::directConnectTree(slow_driver, slow_sink_load);

  auto* root_net = icts::TimingPropagator::genNet("clk_root", source_driver, {fast_load, slow_load});
  auto* fast_net = icts::TimingPropagator::genNet("clk_fast_leaf", fast_driver, {fast_sink_load});
  auto* slow_net = icts::TimingPropagator::genNet("clk_slow_leaf", slow_driver, {slow_sink_load});

  tree.state.driver = source_driver;
  tree.state.nets = {root_net, fast_net, slow_net};
  tree.state.net_records = {{root_net, -1}, {fast_net, -1}, {slow_net, -1}};
  tree.state.buffer_depths.emplace(tree.fast_buf, 0);
  tree.state.buffer_depths.emplace(tree.slow_buf, 0);
  tree.state.buffers_by_depth = {{tree.fast_buf, tree.slow_buf}};
  tree.state.max_depth = 0;

  return tree;
}

PlateauTree buildPlateauUnlockTree()
{
  PlateauTree tree;
  tree.state.net_name = "clk_plateau";

  tree.source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  tree.fast_buf_a = new icts::Inst("fast_buf_a", icts::Point(1000, 0), icts::InstType::kBuffer);
  tree.fast_buf_b = new icts::Inst("fast_buf_b", icts::Point(1000, 1000), icts::InstType::kBuffer);
  tree.slow_buf = new icts::Inst("slow_buf", icts::Point(9000, 0), icts::InstType::kBuffer);
  tree.fast_sink_a = new icts::Inst("fast_sink_a", icts::Point(2000, 0), icts::InstType::kSink);
  tree.fast_sink_b = new icts::Inst("fast_sink_b", icts::Point(2000, 1000), icts::InstType::kSink);
  tree.slow_sink = new icts::Inst("slow_sink", icts::Point(18000, 0), icts::InstType::kSink);

  tree.source->set_cell_master("bufx1");
  tree.fast_buf_a->set_cell_master("bufx2");
  tree.fast_buf_b->set_cell_master("bufx2");
  tree.slow_buf->set_cell_master("bufx1");

  auto* source_driver = tree.source->get_driver_pin();
  auto* fast_load_a = tree.fast_buf_a->get_load_pin();
  auto* fast_load_b = tree.fast_buf_b->get_load_pin();
  auto* slow_load = tree.slow_buf->get_load_pin();
  auto* fast_driver_a = tree.fast_buf_a->get_driver_pin();
  auto* fast_driver_b = tree.fast_buf_b->get_driver_pin();
  auto* slow_driver = tree.slow_buf->get_driver_pin();
  auto* fast_sink_load_a = tree.fast_sink_a->get_load_pin();
  auto* fast_sink_load_b = tree.fast_sink_b->get_load_pin();
  auto* slow_sink_load = tree.slow_sink->get_load_pin();

  icts::TreeBuilder::directConnectTree(source_driver, fast_load_a);
  icts::TreeBuilder::directConnectTree(source_driver, fast_load_b);
  icts::TreeBuilder::directConnectTree(source_driver, slow_load);
  icts::TreeBuilder::directConnectTree(fast_driver_a, fast_sink_load_a);
  icts::TreeBuilder::directConnectTree(fast_driver_b, fast_sink_load_b);
  icts::TreeBuilder::directConnectTree(slow_driver, slow_sink_load);

  auto* root_net = icts::TimingPropagator::genNet("clk_plateau_root", source_driver, {fast_load_a, fast_load_b, slow_load});
  auto* fast_net_a = icts::TimingPropagator::genNet("clk_plateau_fast_a", fast_driver_a, {fast_sink_load_a});
  auto* fast_net_b = icts::TimingPropagator::genNet("clk_plateau_fast_b", fast_driver_b, {fast_sink_load_b});
  auto* slow_net = icts::TimingPropagator::genNet("clk_plateau_slow", slow_driver, {slow_sink_load});

  tree.state.driver = source_driver;
  tree.state.nets = {root_net, fast_net_a, fast_net_b, slow_net};
  tree.state.net_records = {{root_net, -1}, {fast_net_a, -1}, {fast_net_b, -1}, {slow_net, -1}};
  tree.state.buffer_depths.emplace(tree.fast_buf_a, 0);
  tree.state.buffer_depths.emplace(tree.fast_buf_b, 0);
  tree.state.buffer_depths.emplace(tree.slow_buf, 0);
  tree.state.buffers_by_depth = {{tree.fast_buf_a, tree.fast_buf_b, tree.slow_buf}};
  tree.state.max_depth = 0;

  return tree;
}

double rootSkew(const icts::SolverPipelineState& state)
{
  return state.driver == nullptr ? 0.0 : (state.driver->get_max_delay() - state.driver->get_min_delay());
}

void evaluateSimpleTree(icts::SolverPipelineState& state)
{
  ASSERT_GE(state.net_records.size(), 2U);
  for (size_t i = 1; i < state.net_records.size(); ++i) {
    icts::TimingPropagator::update(state.net_records[i].net);
  }
  icts::TimingPropagator::update(state.net_records[0].net);
}

}  // namespace

TEST(SkewPostOptimizationOperatorTest, ReducesRootSkewByUpsizingWorstSlowPathBuffer)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.08);
  guard.runtime->setMaxCap(2.0);
  guard.initTiming();

  const BranchGeometry geometry{1000, 9000, 2000, 18000};
  auto tree = buildTwoBranchTree("bufx2", "bufx1", geometry);
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  icts::SkewPostOptimizationOperator(buildSolverRuntimeContext()).run(tree.state);

  const double updated_skew = rootSkew(tree.state);
  EXPECT_GT(initial_skew, 0.08);
  EXPECT_LT(updated_skew, initial_skew);
  EXPECT_EQ(tree.fast_buf->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx2");
}

TEST(SkewPostOptimizationOperatorTest, DoesNotWorsenSkewWhenNoImprovingDrvSafeMoveExists)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.02);
  guard.runtime->setMaxCap(2.0);
  guard.initTiming();

  const BranchGeometry geometry{1000, 15000, 2000, 30000};
  auto tree = buildTwoBranchTree("bufx2", "bufx2", geometry);
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  icts::SkewPostOptimizationOperator op(buildSolverRuntimeContext());
  op.run(tree.state);

  const double updated_skew = rootSkew(tree.state);
  EXPECT_EQ(tree.fast_buf->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx2");
  EXPECT_GT(initial_skew, 0.02);
  EXPECT_NEAR(updated_skew, initial_skew, 1e-12);
}

TEST(SkewPostOptimizationOperatorTest, KeepsNetRecordsValidAfterAcceptedMove)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.08);
  guard.runtime->setMaxCap(2.0);
  guard.initTiming();

  const BranchGeometry geometry{1000, 9000, 2000, 18000};
  auto tree = buildTwoBranchTree("bufx2", "bufx1", geometry);
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  icts::SkewPostOptimizationOperator(buildSolverRuntimeContext()).run(tree.state);
  const double updated_skew = rootSkew(tree.state);

  EXPECT_EQ(tree.fast_buf->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx2");
  EXPECT_LT(updated_skew, initial_skew);
  EXPECT_TRUE(std::isfinite(tree.state.driver->get_min_delay()));
  EXPECT_TRUE(std::isfinite(tree.state.driver->get_max_delay()));
  EXPECT_LE(tree.state.driver->get_min_delay(), tree.state.driver->get_max_delay());
  EXPECT_EQ(tree.state.net_records.size(), 3U);
  for (const auto& record : tree.state.net_records) {
    ASSERT_NE(record.net, nullptr);
    ASSERT_NE(record.net->get_driver_pin(), nullptr);
    for (auto* load : record.net->get_load_pins()) {
      ASSERT_NE(load, nullptr);
      EXPECT_EQ(load->get_net(), record.net);
    }
  }
}

TEST(SkewPostOptimizationOperatorTest, AllowsPlateauMovesToUnlockLaterImprovement)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.01);
  guard.runtime->setMaxCap(2.0);
  guard.initTiming();

  auto tree = buildPlateauUnlockTree();
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  icts::SkewPostOptimizationOperator(buildSolverRuntimeContext()).run(tree.state);
  const double updated_skew = rootSkew(tree.state);

  EXPECT_GT(initial_skew, 0.01);
  EXPECT_LT(updated_skew, initial_skew);
  EXPECT_EQ(tree.fast_buf_a->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.fast_buf_b->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx2");
}

TEST(SkewPostOptimizationOperatorTest, IgnoresFanoutAndSlewForMoveLegality)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.08);
  guard.runtime->setMaxCap(2.0);
  guard.runtime->setMaxFanout(1);
  guard.runtime->setMaxBufTran(0.0);
  guard.runtime->setMaxSinkTran(0.0);
  guard.initTiming();

  const BranchGeometry geometry{1000, 9000, 2000, 18000};
  auto tree = buildTwoBranchTree("bufx2", "bufx1", geometry);
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  icts::SkewPostOptimizationOperator(buildSolverRuntimeContext()).run(tree.state);
  const double updated_skew = rootSkew(tree.state);

  EXPECT_GT(initial_skew, 0.08);
  EXPECT_LT(updated_skew, initial_skew);
  EXPECT_EQ(tree.fast_buf->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx2");
}

TEST(SkewPostOptimizationOperatorTest, RejectsMoveWhenParentNetCapOverflowWorsens)
{
  RuntimeGuard guard;
  guard.runtime->setSkewBound(0.08);
  guard.runtime->setMaxCap(0.06);
  guard.initTiming();

  const BranchGeometry geometry{1000, 9000, 2000, 18000};
  auto tree = buildTwoBranchTree("bufx2", "bufx1", geometry);
  evaluateSimpleTree(tree.state);
  const double initial_skew = rootSkew(tree.state);
  const double initial_root_cap = tree.state.driver->get_cap_load();
  icts::SkewPostOptimizationOperator(buildSolverRuntimeContext()).run(tree.state);
  const double updated_skew = rootSkew(tree.state);
  const double updated_root_cap = tree.state.driver->get_cap_load();

  EXPECT_GT(initial_skew, 0.08);
  EXPECT_NEAR(updated_skew, initial_skew, 1e-12);
  EXPECT_NEAR(updated_root_cap, initial_root_cap, 1e-12);
  EXPECT_EQ(tree.fast_buf->get_cell_master(), "bufx2");
  EXPECT_EQ(tree.slow_buf->get_cell_master(), "bufx1");
}
