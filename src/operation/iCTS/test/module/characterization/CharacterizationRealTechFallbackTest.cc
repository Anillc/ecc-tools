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
 * @file CharacterizationRealTechFallbackTest.cc
 * @brief Asset-dependent fallback and table-axis coverage on real-tech assets.
 */

#include <gtest/gtest.h>

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "module/characterization/CharBuilder.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

namespace realtech_support = characterization::realtech;

TEST(CharacterizationRealTechFallbackTest, WireLengthUnitFallsBackToStrongestBufferHeight)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("fallback_wire_length_unit", std::nullopt, 0.0, 0.0, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  const auto buffer_infos = realtech_support::CollectConfiguredBufferLimitInfo();
  const auto usable_buffers = realtech_support::CollectUsableBufferMasters(buffer_infos);
  if (usable_buffers.empty()) {
    GTEST_SKIP() << "No configured buffer has both drive-cap data and physical height in real-tech assets.";
  }

  const double expected_unit_um = realtech_support::ResolveDefaultWireLengthUnitUm(buffer_infos, usable_buffers);
  ASSERT_GT(expected_unit_um, 0.0);

  icts::CharBuilder builder;
  builder.init();
  EXPECT_DOUBLE_EQ(builder.get_wire_length_unit_um(), expected_unit_um);
  EXPECT_EQ(builder.get_wire_length_iterations(), realtech_support::kRealTechCharWireLengthIterations);
}

TEST(CharacterizationRealTechFallbackTest, TableAxisFallbackMatchesAvailableAssetCoverage)
{
  std::vector<realtech_support::BufferLimitInfo> buffer_infos;
  {
    realtech_support::RealTechCharSession baseline_session;
    if (const auto prepare_error = baseline_session.prepare("fallback_inventory", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
      GTEST_SKIP() << *prepare_error;
      return;
    }

    buffer_infos = realtech_support::CollectConfiguredBufferLimitInfo();
  }

  if (buffer_infos.empty()) {
    GTEST_SKIP() << "No configured buffers with resolvable input and output pins in real-tech assets.";
  }

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(6);
  report_stream << "scenario=fallback_table_axis\n";
  report_stream << "buffer_inventory_begin\n";
  for (const auto& info : buffer_infos) {
    report_stream << "buffer{cell=" << info.cell_master << ",input_pin=" << info.input_pin << ",output_pin=" << info.output_pin
                  << ",port_slew_limit_ns=" << info.port_slew_limit_ns << ",table_slew_limit_ns=" << info.table_slew_limit_ns
                  << ",port_cap_limit_pf=" << info.port_cap_limit_pf << ",table_cap_limit_pf=" << info.table_cap_limit_pf << "}\n";
  }
  report_stream << "buffer_inventory_end\n";

  bool exercised = false;
  std::vector<std::string> limitation_notes;

  const auto cap_table_only_buffers = realtech_support::CollectMastersByPredicate(buffer_infos, [](const auto& info) -> bool {
    const bool has_cap_table_only = info.port_cap_limit_pf <= 0.0 && info.table_cap_limit_pf > 0.0;
    const bool has_any_slew = info.port_slew_limit_ns > 0.0 || info.table_slew_limit_ns > 0.0;
    return has_cap_table_only && has_any_slew;
  });
  if (!cap_table_only_buffers.empty()) {
    realtech_support::RealTechCharSession cap_session;
    const auto prepare_error = cap_session.prepare("fallback_cap_table_only", cap_table_only_buffers, 0.1, 0.0);
    ASSERT_FALSE(prepare_error.has_value()) << (prepare_error.has_value() ? *prepare_error : "");

    icts::CharBuilder builder;
    builder.init();
    const double expected_cap = realtech_support::MinPositiveResolvedLimit(buffer_infos, cap_table_only_buffers, false);
    ASSERT_GT(expected_cap, 0.0);
    EXPECT_DOUBLE_EQ(builder.get_max_cap(), expected_cap);
    report_stream << "cap_table_only_buffers=" << realtech_support::JoinStrings(cap_table_only_buffers) << "\n";
    report_stream << "cap_table_only_expected_max_cap_pf=" << expected_cap << "\n";
    exercised = true;
  } else {
    limitation_notes.emplace_back("cap_table_only_buffers_unavailable");
  }

  const auto slew_table_only_buffers = realtech_support::CollectMastersByPredicate(buffer_infos, [](const auto& info) -> bool {
    const bool has_slew_table_only = info.port_slew_limit_ns <= 0.0 && info.table_slew_limit_ns > 0.0;
    const bool has_any_cap = info.port_cap_limit_pf > 0.0 || info.table_cap_limit_pf > 0.0;
    return has_slew_table_only && has_any_cap;
  });
  if (!slew_table_only_buffers.empty()) {
    realtech_support::RealTechCharSession slew_session;
    const auto prepare_error = slew_session.prepare("fallback_slew_table_only", slew_table_only_buffers, 0.0, 1.0);
    ASSERT_FALSE(prepare_error.has_value()) << (prepare_error.has_value() ? *prepare_error : "");

    icts::CharBuilder builder;
    builder.init();
    const double expected_slew = realtech_support::MinPositiveResolvedLimit(buffer_infos, slew_table_only_buffers, true);
    ASSERT_GT(expected_slew, 0.0);
    EXPECT_DOUBLE_EQ(builder.get_max_slew(), expected_slew);
    report_stream << "slew_table_only_buffers=" << realtech_support::JoinStrings(slew_table_only_buffers) << "\n";
    report_stream << "slew_table_only_expected_max_slew_ns=" << expected_slew << "\n";
    exercised = true;
  } else {
    limitation_notes.emplace_back("slew_table_only_buffers_unavailable");
  }

  report_stream << "fallback_exercised=" << (exercised ? "true" : "false") << "\n";
  report_stream << "limitations=" << realtech_support::JoinStrings(limitation_notes) << "\n";
  ASSERT_TRUE(realtech_support::WriteScenarioLog("fallback_table_axis", "fallback_table_axis_report.txt", report_stream.str()));

  if (!exercised) {
    GTEST_SKIP() << "Current real-tech assets cannot directly exercise table-axis-only fallback. "
                 << realtech_support::JoinStrings(limitation_notes);
  }
}

}  // namespace
}  // namespace icts_test
