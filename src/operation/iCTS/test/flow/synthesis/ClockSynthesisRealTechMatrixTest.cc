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
 * @file ClockSynthesisRealTechMatrixTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech BP placement matrix coverage for ClockSynthesis flow.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ClockSynthesisRealTechSmokeSupport.hh"

namespace icts_test {
namespace {

using namespace synthesis_realtech_smoke;

TEST(ClockSynthesisRealTechSmokeTest, BpBeTopFullSinkNonClusteredExperimentMatrix)
{
  const auto matrix_result = EvaluateBpBeTopFullSinkNonClusteredExperimentMatrix();
  if (matrix_result.skipped) {
    GTEST_SKIP() << matrix_result.skip_reason;
    return;
  }

  for (const auto& failure_message : matrix_result.failure_messages) {
    ADD_FAILURE() << failure_message;
  }
  EXPECT_TRUE(matrix_result.report_written);
}

}  // namespace
}  // namespace icts_test
