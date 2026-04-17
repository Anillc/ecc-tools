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
 * @file TopologyBuilderOperatorTest.cc
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
#include "LongWireBufferingOperator.hh"
#include "Net.hh"
#include "TimingPropagator.hh"
#include "TopologyBuilderOperator.hh"
#include "TreeBuilder.hh"
#include "gtest/gtest.h"

namespace {

class TestCtsRuntime final : public icts::CtsRuntimeInterface
{
 public:
  TestCtsRuntime()
  {
    _config.set_max_cap(0.5);
    _config.set_max_fanout(8);
    _config.set_skew_bound(1.0);
    _config.set_work_dir("/tmp");

    std::vector<std::vector<double>> index_list = {{0.0, 1.0}, {0.0, 1.0}};
    std::vector<double> delay_values = {0.1, 0.1, 0.1, 0.1};
    std::vector<double> slew_values = {0.1, 0.1, 0.1, 0.1};
    _buffer_lib = std::make_unique<icts::CtsCellLib>("bufx1", index_list, delay_values, slew_values);
    _buffer_lib->set_delay_coef({0.0, 0.0, 0.0});
    _buffer_lib->set_slew_coef({0.0, 0.0});
    _buffer_lib->set_init_cap(0.02);
    _buffer_lib->set_area(1.0);
    _buffer_lib->set_leakage_power(0.01);
    _cell_libs.emplace(_buffer_lib->get_cell_master(), _buffer_lib.get());
    _buffer_libs.push_back(_buffer_lib.get());
  }

  void setMaxCap(double max_cap) { _config.set_max_cap(max_cap); }
  void setMaxFanout(int max_fanout) { _config.set_max_fanout(max_fanout); }

  icts::CtsConfig* getConfig() const override { return const_cast<icts::CtsConfig*>(&_config); }
  icts::CtsDesign* getDesign() const override { return nullptr; }
  icts::CtsDBWrapper* getDbWrapper() const override { return nullptr; }

  double getClockUnitCap(const std::optional<icts::LayerPattern>& layer_pattern = std::nullopt) const override
  {
    (void) layer_pattern;
    return 0.01;
  }
  double getClockUnitRes(const std::optional<icts::LayerPattern>& layer_pattern = std::nullopt) const override
  {
    (void) layer_pattern;
    return 1.0;
  }
  double getSinkCap(const std::string& load_pin_full_name) const override
  {
    (void) load_pin_full_name;
    return 0.0;
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
  icts::CtsConfig _config;
  std::unique_ptr<icts::CtsCellLib> _buffer_lib;
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
    runtime = std::make_shared<TestCtsRuntime>();
    icts::CtsRuntimeRegistry::install(runtime);
    icts::TimingPropagator::init();
  }

  ~RuntimeGuard() { icts::CtsRuntimeRegistry::clear(); }

  std::shared_ptr<TestCtsRuntime> runtime;
};

icts::SolverRuntimeContext buildSolverRuntimeContext()
{
  icts::SolverRuntimeContext runtime;
  runtime.min_buffering_length_provider = []() { return 100.0; };
  runtime.cell_lib_exist_provider = [](const std::string& cell_master) { return cell_master == "bufx1"; };
  runtime.gen_id_provider = []() {
    static size_t next_id = 0;
    return ++next_id;
  };
  runtime.save_log = [](const std::string& text) { (void) text; };
  runtime.cell_area_provider = [](const std::string& cell_master) {
    (void) cell_master;
    return 1.0;
  };
  runtime.cell_leakage_power_provider = [](const std::string& cell_master) {
    (void) cell_master;
    return 0.01;
  };
  return runtime;
}

icts::Pin* createLeafLoad(const std::string& name, const icts::Point& location)
{
  auto* leaf_buf = icts::TreeBuilder::genBufInst(name, location);
  leaf_buf->set_cell_master("bufx1");
  auto* leaf_load = leaf_buf->get_load_pin();
  icts::TimingPropagator::updatePinCap(leaf_load);
  icts::TimingPropagator::initLoadPinDelay(leaf_load);
  return leaf_load;
}

TEST(TopologyBuilderOperatorTest, StopsBinaryPartitionWhenSteinerCapFitsWithinMaxCap)
{
  RuntimeGuard runtime_guard;
  auto runtime = buildSolverRuntimeContext();

  icts::SolverPipelineState state;
  state.net_name = "clk";

  auto* source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  source->set_cell_master("bufx1");
  state.driver = source->get_driver_pin();

  state.leaf_load_pins = {
      createLeafLoad("leaf0", icts::Point(1000, 1000)),
      createLeafLoad("leaf1", icts::Point(1200, 1100)),
      createLeafLoad("leaf2", icts::Point(900, 1300)),
      createLeafLoad("leaf3", icts::Point(1100, 1400)),
  };

  icts::TopologyBuilderOperator(runtime).run(state);

  ASSERT_EQ(state.buffers_by_depth.size(), 2U);
  EXPECT_EQ(state.max_depth, 1);
  EXPECT_EQ(state.buffers_by_depth[0].size(), 1U);
  EXPECT_EQ(state.buffers_by_depth[1].size(), state.leaf_load_pins.size());
  EXPECT_EQ(state.buffer_depths.size(), state.leaf_load_pins.size() + 1U);

  size_t branch_buffer_count = 0;
  for (const auto& [buffer, depth] : state.buffer_depths) {
    (void) depth;
    if (buffer != nullptr && buffer->get_name().find("_branch_") != std::string::npos) {
      ++branch_buffer_count;
    }
  }
  EXPECT_EQ(branch_buffer_count, 0U);

  ASSERT_EQ(state.nets.size(), 2U);
  ASSERT_EQ(state.net_records.size(), 2U);

  auto* root_buffer = state.buffers_by_depth[0].front();
  ASSERT_NE(root_buffer, nullptr);
  auto* steiner_net = state.nets[1];
  ASSERT_NE(steiner_net, nullptr);
  EXPECT_EQ(steiner_net->get_driver_pin(), root_buffer->get_driver_pin());
  EXPECT_EQ(steiner_net->get_load_pins().size(), state.leaf_load_pins.size());
  EXPECT_NE(steiner_net->get_name().find("_steiner_"), std::string::npos);
  EXPECT_FALSE(state.net_records[1].allow_long_wire_buffering);

  for (icts::Pin* leaf_load : state.leaf_load_pins) {
    ASSERT_NE(leaf_load, nullptr);
    auto depth_it = state.buffer_depths.find(leaf_load->get_inst());
    ASSERT_NE(depth_it, state.buffer_depths.end());
    EXPECT_EQ(depth_it->second, 1);
  }

  const size_t buffer_count_before_break = state.buffer_depths.size();
  const size_t net_record_count_before_break = state.net_records.size();
  state.min_buffering_length = 0.1;
  icts::LongWireBufferingOperator(runtime).run(state);
  EXPECT_EQ(state.buffer_depths.size(), buffer_count_before_break);
  EXPECT_EQ(state.net_records.size(), net_record_count_before_break);
}

TEST(TopologyBuilderOperatorTest, KeepsBinaryPartitionWhenSteinerCapReachesMaxCap)
{
  RuntimeGuard runtime_guard;
  auto runtime = buildSolverRuntimeContext();

  icts::SolverPipelineState state;
  state.net_name = "clk";

  auto* source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  source->set_cell_master("bufx1");
  state.driver = source->get_driver_pin();

  state.leaf_load_pins = {
      createLeafLoad("leaf0", icts::Point(1000, 1000)),
      createLeafLoad("leaf1", icts::Point(1200, 1100)),
      createLeafLoad("leaf2", icts::Point(900, 1300)),
      createLeafLoad("leaf3", icts::Point(1100, 1400)),
  };

  double pin_cap_sum = 0.0;
  for (icts::Pin* leaf_load : state.leaf_load_pins) {
    pin_cap_sum += leaf_load->get_cap_load();
  }
  const double steiner_wire_cap
      = icts::TreeBuilder::estimateFluteWireLength(state.driver, state.leaf_load_pins) * icts::TimingPropagator::getUnitCap();
  runtime_guard.runtime->setMaxCap(pin_cap_sum + steiner_wire_cap);
  icts::TimingPropagator::init();

  icts::TopologyBuilderOperator(runtime).run(state);

  size_t branch_buffer_count = 0;
  for (const auto& [buffer, depth] : state.buffer_depths) {
    (void) depth;
    if (buffer != nullptr && buffer->get_name().find("_branch_") != std::string::npos) {
      ++branch_buffer_count;
    }
  }
  EXPECT_GT(branch_buffer_count, 0U);
  EXPECT_GT(state.buffer_depths.size(), state.leaf_load_pins.size() + 1U);
}

TEST(TopologyBuilderOperatorTest, KeepsBinaryPartitionWhenLoadCountReachesMaxFanout)
{
  RuntimeGuard runtime_guard;
  auto runtime = buildSolverRuntimeContext();

  icts::SolverPipelineState state;
  state.net_name = "clk";

  auto* source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  source->set_cell_master("bufx1");
  state.driver = source->get_driver_pin();

  state.leaf_load_pins = {
      createLeafLoad("leaf0", icts::Point(1000, 1000)),
      createLeafLoad("leaf1", icts::Point(1200, 1100)),
      createLeafLoad("leaf2", icts::Point(900, 1300)),
      createLeafLoad("leaf3", icts::Point(1100, 1400)),
  };

  runtime_guard.runtime->setMaxFanout(static_cast<int>(state.leaf_load_pins.size()));
  icts::TimingPropagator::init();

  const double steiner_wire_cap
      = icts::TreeBuilder::estimateFluteWireLength(state.driver, state.leaf_load_pins) * icts::TimingPropagator::getUnitCap();
  double pin_cap_sum = 0.0;
  for (icts::Pin* leaf_load : state.leaf_load_pins) {
    pin_cap_sum += leaf_load->get_cap_load();
  }
  ASSERT_LT(pin_cap_sum + steiner_wire_cap, icts::TimingPropagator::getMaxCap());

  icts::TopologyBuilderOperator(runtime).run(state);

  size_t branch_buffer_count = 0;
  for (const auto& [buffer, depth] : state.buffer_depths) {
    (void) depth;
    if (buffer != nullptr && buffer->get_name().find("_branch_") != std::string::npos) {
      ++branch_buffer_count;
    }
  }
  EXPECT_GT(branch_buffer_count, 0U);
  EXPECT_GT(state.buffer_depths.size(), state.leaf_load_pins.size() + 1U);
}

}  // namespace
