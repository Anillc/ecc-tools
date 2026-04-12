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

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <unistd.h>
#include <vector>

#include "../../../interface/python/py_imp/idb_to_imp_db/LibertyExportUtils.hh"
#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "string/Str.hh"

using ieda::Log;
using ista::Lib;

using namespace ista;

namespace {

class ScopedTempFile {
 public:
  explicit ScopedTempFile(const std::string& contents) {
    auto file_name = std::filesystem::temp_directory_path() /
                     std::filesystem::path("ieda_liberty_test_XXXXXX.lib");
    std::string templ = file_name.string();
    std::vector<char> writable_path(templ.begin(), templ.end());
    writable_path.push_back('\0');
    int fd = mkstemps(writable_path.data(), 4);
    if (fd == -1) {
      throw std::runtime_error("mkstemps failed");
    }
    close(fd);
    _path = writable_path.data();

    std::ofstream out(_path);
    out << contents;
  }

  ~ScopedTempFile() {
    if (!_path.empty()) {
      std::error_code ec;
      std::filesystem::remove(_path, ec);
    }
  }

  const char* c_str() const { return _path.c_str(); }

 private:
  std::string _path;
};

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

TEST_F(LibertyTest, parser_keeps_slew_derate_and_pin_slew_limit_in_their_fields) {
  const std::string lib_text = R"(library(test_lib) {
    time_unit : "1ps";
    capacitive_load_unit (1,ff);
    slew_derate_from_library : 2.5;
    default_max_transition : 320;
    cell(TESTX1) {
      area : 1.0;
      pin(A) {
        direction : input;
        capacitance : 1.5;
        rise_capacitance : 1.6;
        fall_capacitance : 1.7;
        max_capacitance : 2.0;
        max_transition : 123;
      }
      pin(Y) {
        direction : output;
        function : "A";
      }
    }
  })";

  ScopedTempFile lib_file(lib_text);

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_file.c_str());
  ASSERT_EQ(lib_rust_reader.linkLib(), 1U);
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  EXPECT_DOUBLE_EQ(lib_library->get_slew_derate_from_library(), 2.5);
  ASSERT_TRUE(lib_library->get_default_max_transition().has_value());
  EXPECT_DOUBLE_EQ(*lib_library->get_default_max_transition(), 0.32);

  auto* cell = lib_library->findCell("TESTX1");
  ASSERT_NE(cell, nullptr);
  auto* port = cell->get_cell_port_or_port_bus("A");
  ASSERT_NE(port, nullptr);

  auto slew_limit = port->get_port_slew_limit(AnalysisMode::kMax);
  ASSERT_TRUE(slew_limit.has_value());
  EXPECT_DOUBLE_EQ(*slew_limit, 0.123);

  auto cap_limit = port->get_port_cap_limit(AnalysisMode::kMax);
  ASSERT_TRUE(cap_limit.has_value());
  EXPECT_DOUBLE_EQ(*cap_limit, 0.002);
}

TEST_F(LibertyTest, py_imp_exports_internal_pin_cap_without_extra_unit_scaling) {
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_lib_pin_cap_for_python_pf(0.002),
      0.002);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_lib_pin_cap_limit_for_python_pf(0.003,
                                                                          0.1),
      0.003);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_lib_pin_cap_limit_for_python_pf(
          std::nullopt, 0.1),
      0.1);
}

TEST_F(LibertyTest, py_imp_exports_internal_slew_and_one_dimensional_lut_values_on_time_basis) {
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_lib_pin_slew_limit_for_python_ps(
          0.123, 0.0),
      123.0);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_one_dimensional_lut_axis_for_python(
          0.4, true, 1000.0, 0.001),
      400.0);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_one_dimensional_lut_axis_for_python(
          0.4, false, 1000.0, 0.001),
      0.0004);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_lut_time_value_for_python_ps(
          0.4, 1000.0),
      400.0);
}

TEST_F(LibertyTest, py_imp_falls_back_to_library_default_slew_limit_for_output_pin_export) {
  const std::string lib_text = R"(library(test_default_slew) {
    time_unit : "1ps";
    default_max_transition : 320;
    cell(TESTINVX1) {
      area : 1.0;
      pin(A) {
        direction : input;
        capacitance : 1.5;
        max_transition : 123;
      }
      pin(Y) {
        direction : output;
        function : "!A";
      }
    }
  })";

  ScopedTempFile lib_file(lib_text);

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_file.c_str());
  ASSERT_EQ(lib_rust_reader.linkLib(), 1U);
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  auto* cell = lib_library->findCell("TESTINVX1");
  ASSERT_NE(cell, nullptr);
  auto* input_port = cell->get_cell_port_or_port_bus("A");
  auto* output_port = cell->get_cell_port_or_port_bus("Y");
  ASSERT_NE(input_port, nullptr);
  ASSERT_NE(output_port, nullptr);

  ASSERT_TRUE(lib_library->get_default_max_transition().has_value());
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::resolve_lib_pin_slew_limit_for_python_ps(
          input_port->get_port_slew_limit(AnalysisMode::kMax),
          lib_library->get_default_max_transition()),
      123.0);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::resolve_lib_pin_slew_limit_for_python_ps(
          output_port->get_port_slew_limit(AnalysisMode::kMax),
          lib_library->get_default_max_transition()),
      320.0);
}

TEST_F(LibertyTest, parser_converts_wireload_resistance_and_power_units) {
  const std::string lib_text = R"(library(test_units) {
    time_unit : "1ps";
    capacitive_load_unit (1,ff);
    pulling_resistance_unit : "1ohm";
    leakage_power_unit : "1pW";
    wire_load(WL) {
      resistance : 2.5;
    }
    cell(TESTX1) {
      area : 1.0;
      cell_leakage_power : 7.0;
      leakage_power() {
        value : 3.0;
      }
      pin(A) {
        direction : input;
        capacitance : 1.5;
      }
      pin(Y) {
        direction : output;
        function : "A";
      }
    }
  })";

  ScopedTempFile lib_file(lib_text);

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_file.c_str());
  ASSERT_EQ(lib_rust_reader.linkLib(), 1U);
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  auto* wire_load = lib_library->getWireLoad("WL");
  ASSERT_NE(wire_load, nullptr);
  ASSERT_TRUE(wire_load->get_resistance_per_length_unit().has_value());
  EXPECT_DOUBLE_EQ(*wire_load->get_resistance_per_length_unit(), 2.5);

  auto* cell = lib_library->findCell("TESTX1");
  ASSERT_NE(cell, nullptr);
  EXPECT_DOUBLE_EQ(cell->get_cell_leakage_power(), 7e-9);
  EXPECT_DOUBLE_EQ(cell->convertTablePowerToMw(2.0), 2e-9);

  auto leakage_powers = cell->getLeakagePowerList();
  ASSERT_EQ(leakage_powers.size(), 1U);
  EXPECT_DOUBLE_EQ(leakage_powers.front()->get_value(), 3e-9);
}

TEST_F(LibertyTest, export_uses_conditional_leakage_when_scalar_cell_leakage_is_missing) {
  const std::string lib_text = R"(library(test_conditional_leakage) {
    time_unit : "1ps";
    leakage_power_unit : "1pW";
    default_cell_leakage_power : 0;
    cell(TESTX1) {
      area : 1.0;
      leakage_power() {
        value : 8.0;
        when : "A";
        related_pg_pin : VDD;
      }
      leakage_power() {
        value : 4.0;
        when : "!A";
        related_pg_pin : VDD;
      }
      leakage_power() {
        value : 0.0;
        related_pg_pin : VSS;
      }
      pin(A) {
        direction : input;
        capacitance : 1.5;
      }
      pin(Y) {
        direction : output;
        function : "A";
      }
    }
  })";

  ScopedTempFile lib_file(lib_text);

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_file.c_str());
  ASSERT_EQ(lib_rust_reader.linkLib(), 1U);
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  auto* cell = lib_library->findCell("TESTX1");
  ASSERT_NE(cell, nullptr);
  EXPECT_DOUBLE_EQ(cell->get_cell_leakage_power(), 0.0);

  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_libcell_leakage_for_python_mw(*cell),
      6e-9);
}

TEST_F(LibertyTest, export_ignores_leakage_group_without_value) {
  const std::string lib_text = R"(library(test_missing_leakage_value) {
    time_unit : "1ps";
    leakage_power_unit : "1pW";
    default_cell_leakage_power : 0;
    cell(TESTX1) {
      area : 1.0;
      leakage_power() {
        related_pg_pin : VDD;
        when : "A";
      }
      leakage_power() {
        value : 4.0;
        related_pg_pin : VDD;
        when : "!A";
      }
      pin(A) {
        direction : input;
        capacitance : 1.5;
      }
      pin(Y) {
        direction : output;
        function : "A";
      }
    }
  })";

  ScopedTempFile lib_file(lib_text);

  Lib lib;
  auto lib_rust_reader = lib.loadLibertyWithRustParser(lib_file.c_str());
  ASSERT_EQ(lib_rust_reader.linkLib(), 1U);
  auto lib_library = lib_rust_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  auto* cell = lib_library->findCell("TESTX1");
  ASSERT_NE(cell, nullptr);

  auto leakage_powers = cell->getLeakagePowerList();
  ASSERT_EQ(leakage_powers.size(), 2U);
  EXPECT_DOUBLE_EQ(leakage_powers[1]->get_value(), 4e-9);
  EXPECT_DOUBLE_EQ(
      python_interface::pydb_test::export_libcell_leakage_for_python_mw(*cell),
      4e-9);
}

}  // namespace
