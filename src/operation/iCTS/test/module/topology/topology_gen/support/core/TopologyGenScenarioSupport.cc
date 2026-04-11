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
 * @file TopologyGenScenarioSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Thin scenario runner for topology generation tests.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/TopologyGen.hh"
#include "module/topology/topology_gen/support/TopologyGenInternal.hh"
#include "module/topology/topology_gen/support/TopologyGenShared.hh"
#include "utils/logger/Logger.hh"

namespace icts_test::topology_gen {

static_assert(std::is_same_v<decltype(TopologyCase::name), std::string>);

auto RunBuildAndVisualize(const TopologyCase& test_case) -> void
{
  const auto output_dir = common::io::ResolveTopologyGenOutputDir();
  detail::ValidateOutputDir(output_dir);

  const auto log_path = output_dir / ("topology_" + test_case.name + ".log");
  const common::logging::ScopedLogFile log_guard(log_path);

  CTS_LOG_INFO << "Topology test start: " << test_case.name << ", count=" << test_case.count << ", seed=" << test_case.seed;

  auto data = detail::GenerateCase(test_case);
  ASSERT_EQ(data.loads.size(), test_case.count);

  const auto tree = icts::TopologyGen::build(data.loads);
  detail::TopologyArtifacts artifacts;
  detail::AnalyzeBuiltTopology(tree, data.loads, artifacts);
  detail::LogTopologySummary(artifacts.stats);
  detail::ValidateTreeEdges(tree);
  detail::WriteArtifacts(output_dir, test_case, tree, data.loads);
}

}  // namespace icts_test::topology_gen
