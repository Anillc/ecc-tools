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

// #include <gperftools/heap-profiler.h>

#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "string/Str.hh"

using ieda::Log;
using ista::Lib;

using namespace ista;

namespace {

class LibertyTest : public testing::Test {
  void SetUp() {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() { Log::end(); }
};

TEST_F(LibertyTest, rust_reader) {
  const char* lib_path =
      "/home/ieda/ssta-data/lib/lib/tcbn28hpcplusbwp30p140ulvtssg0p81v125c.lib";
  Lib lib;
  lib.loadLibertyWithRustParser(lib_path);
}

TEST_F(LibertyTest, rust_expr_builder) {
  RustLibertyExprBuilder expr_builder("(!((A1 A2)+(B1 B2)))");
  expr_builder.execute();
  auto* func_expr = expr_builder.get_result_expr();
  LOG_FATAL_IF(!func_expr) << "func_expr is nullptr";
}

TEST_F(LibertyTest, rust_expr_builder_backslack) {
  RustLibertyExprBuilder expr_builder(R"((!CEN & ! \
                                   WEN & !( \
                                (BWEN[0]) & \
                                (BWEN[1]) & \
                                (BWEN[2]) & \
                                (BWEN[3]) & \
                                (BWEN[4]) & \
                                (BWEN[5]) & \
                                (BWEN[6]) & \
                                (BWEN[7]) & \
                                (BWEN[8]) & \
                                (BWEN[9]) & \
                                (BWEN[10]) & \
                                (BWEN[11]) & \
                                (BWEN[12]) & \
                                (BWEN[13]) & \
                                (BWEN[14]) & \
                                (BWEN[15]) & \
                                (BWEN[16]) & \
                                (BWEN[17]) & \
                                (BWEN[18]) & \
                                (BWEN[19]) & \
                                (BWEN[20]) & \
                                (BWEN[21]) & \
                                (BWEN[22]) & \
                                (BWEN[23]) & \
                                (BWEN[24]) & \
                                (BWEN[25]) & \
                                (BWEN[26]) & \
                                (BWEN[27]) & \
                                (BWEN[28]) & \
                                (BWEN[29]) & \
                                (BWEN[30]) & \
                                (BWEN[31])) \
                                ) \
                                 )");
  expr_builder.execute();
  auto* func_expr = expr_builder.get_result_expr();
  LOG_FATAL_IF(!func_expr) << "func_expr is nullptr";
}

TEST_F(LibertyTest, print_liberty_library_json) {
  const char* lib_path =
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib";
  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_path);
  lib_rust_reader.linkLib();
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  // lib_library->findCell()->get_cell_arcs();
  // const char* json_file_names_n45 =
  //     "/home/longshuaiying/lib_lef/"
  //     "NangateOpenCellLibrary_typical.json";
  // lib_library->printLibertyLibraryJson(json_file_names_n45);
}

TEST_F(LibertyTest, rust_reader_converts_ff_pin_cap_ranges_to_internal_pf) {
  const char* lib_path =
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/"
      "lib/asap7sc7p5t_SEQ_RVT_FF_nldm_201020.lib";

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_path);
  lib_rust_reader.linkLib();
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();

  ASSERT_NE(lib_library, nullptr);
  EXPECT_EQ(lib_library->get_cap_unit(), CapacitiveUnit::kFF);

  auto* lib_cell = lib_library->findCell("ASYNC_DFFHx1_ASAP7_75t_R");
  ASSERT_NE(lib_cell, nullptr);

  auto* data_port = lib_cell->get_cell_port_or_port_bus("D");
  ASSERT_NE(data_port, nullptr);

  EXPECT_NEAR(data_port->get_port_cap(), 0.000621396, 1e-9);

  const auto max_rise_cap =
      data_port->get_port_cap(AnalysisMode::kMax, TransType::kRise);
  const auto min_rise_cap =
      data_port->get_port_cap(AnalysisMode::kMin, TransType::kRise);
  const auto max_fall_cap =
      data_port->get_port_cap(AnalysisMode::kMax, TransType::kFall);
  const auto min_fall_cap =
      data_port->get_port_cap(AnalysisMode::kMin, TransType::kFall);

  ASSERT_TRUE(max_rise_cap.has_value());
  ASSERT_TRUE(min_rise_cap.has_value());
  ASSERT_TRUE(max_fall_cap.has_value());
  ASSERT_TRUE(min_fall_cap.has_value());

  EXPECT_NEAR(*max_rise_cap, 0.000619712, 1e-9);
  EXPECT_NEAR(*min_rise_cap, 0.000553479, 1e-9);
  EXPECT_NEAR(*max_fall_cap, 0.000621396, 1e-9);
  EXPECT_NEAR(*min_fall_cap, 0.000554061, 1e-9);
}

}  // namespace
