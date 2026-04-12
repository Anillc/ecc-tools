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
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"

using namespace ista;

namespace {

struct TestDbSetup {
  std::unique_ptr<idb::IdbBuilder> db_builder;
  idb::IdbDesign* idb_design = nullptr;
  ista::TimingEngine* timing_engine = nullptr;
  ista::TimingIDBAdapter* db_adapter = nullptr;
};

TestDbSetup buildTimingDb() {
  constexpr const char* kRepoRoot =
      "/home/zhaoxueyan/papers/PhD_zhongqi/third_party/benchmark-admm-gatesizing";
  const std::string lef_file =
      std::string(kRepoRoot) +
      "/AiEDA/third_party/sizer/thirdparty/OpenROAD/test/Nangate45/Nangate45.lef";
  const std::string def_file =
      std::string(kRepoRoot) +
      "/AiEDA/third_party/sizer/thirdparty/OpenROAD/test/gcd_nangate45.def";
  const std::string lib_file =
      std::string(kRepoRoot) +
      "/AiEDA/third_party/sizer/thirdparty/OpenROAD/test/Nangate45/Nangate45_typ.lib";

  std::vector<std::string> lef_files{lef_file};
  std::vector<std::string> lib_files{lib_file};

  TestDbSetup setup;
  setup.db_builder = std::make_unique<idb::IdbBuilder>();
  EXPECT_TRUE(setup.db_builder->buildLef(lef_files));
  EXPECT_TRUE(setup.db_builder->buildDef(def_file));

  setup.idb_design = setup.db_builder->get_def_service()->get_design();
  EXPECT_NE(setup.idb_design, nullptr);

  setup.timing_engine = TimingEngine::getOrCreateTimingEngine();
  setup.timing_engine->readLiberty(lib_files);
  setup.timing_engine->setDefDesignBuilder(setup.db_builder.get());

  setup.db_adapter =
      dynamic_cast<TimingIDBAdapter*>(setup.timing_engine->get_db_adapter());
  EXPECT_NE(setup.db_adapter, nullptr);

  return setup;
}

class DBToNetlistTest : public testing::Test {
 protected:
  void SetUp() override {
    char config[] = "test";
    char* argv[] = {config};
    ieda::Log::init(argv);
    ista::TimingEngine::destroyTimingEngine();
  }

  void TearDown() override {
    ista::TimingEngine::destroyTimingEngine();
    ieda::Log::end();
  }
};

TEST_F(DBToNetlistTest, substituteCellShouldSyncStaInstanceMaster) {
  auto setup = buildTimingDb();

  auto* target_idb_inst =
      setup.idb_design->get_instance_list()->find_instance("_348_");
  ASSERT_NE(target_idb_inst, nullptr);
  ASSERT_EQ(std::string(target_idb_inst->get_cell_master()->get_name()),
            "INV_X1");

  auto* target_sta_inst = setup.db_adapter->dbToSta(target_idb_inst);
  ASSERT_NE(target_sta_inst, nullptr);
  ASSERT_EQ(std::string(target_sta_inst->get_inst_cell()->get_cell_name()),
            "INV_X1");

  auto* target_lib_cell = setup.timing_engine->get_ista()->findLibertyCell("INV_X2");
  ASSERT_NE(target_lib_cell, nullptr);
  setup.db_adapter->substituteCell(target_sta_inst, target_lib_cell);

  EXPECT_EQ(std::string(target_idb_inst->get_cell_master()->get_name()),
            "INV_X2");
  EXPECT_EQ(std::string(target_sta_inst->get_inst_cell()->get_cell_name()),
            "INV_X2");
}

}  // namespace
