// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"
#include "sta/StaCharacterTiming.hh"
#include "usage/usage.hh"

using namespace ista;
using namespace ieda;

namespace {

class CharacterTimingTest : public testing::Test {
  void SetUp() final {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() final { Log::end(); }
};

TEST_F(CharacterTimingTest, example1) {
  Stats stats;

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(48);
  const char* design_work_space = "/home/longshuaiying/cluster_timing_model";
  timing_engine->set_design_work_space(design_work_space);

  std::vector<const char*> lib_files{
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib"};
  timing_engine->readLiberty(lib_files);

  timing_engine->get_ista()->set_analysis_mode(ista::AnalysisMode::kMaxMin);
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);

  timing_engine->get_ista()->set_top_module_name("cluster2");

  timing_engine->readDesign(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "hier_sub_netlist2.v");

  // timing_engine->readSdc(
  //     "/home/taosimin/nangate45/design/example/example1.sdc");

  timing_engine->buildGraph();

  timing_engine->extractTimingModel(AnalysisMode::kMax, "example1_v.lib");
  // timing_engine->extractTimingModel(AnalysisMode::kMin,
  // "macro_model_min.lib");

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  testing::GTEST_FLAG(filter) = "CharacterTimingTest.*";
  return RUN_ALL_TESTS();
}
