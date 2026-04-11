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
 * @file PatternHashTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Pattern-id and hash helper tests for characterization.
 */

#include <gtest/gtest.h>

#include <string>

#include "database/characterization/PatternId.hh"
#include "module/characterization/HashJoinEngine.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

TEST(PatternIdTest, DomainTagging)
{
  auto seg_id = icts::PatternId::segment(support::kPatternId42);
  auto topo_id = icts::PatternId::topology(support::kPatternId42);

  EXPECT_EQ(seg_id.domain, icts::PatternDomain::kSegmentPattern);
  EXPECT_EQ(topo_id.domain, icts::PatternDomain::kTopologyPattern);
  EXPECT_EQ(seg_id.local_id, support::kPatternId42);
  EXPECT_EQ(topo_id.local_id, support::kPatternId42);

  EXPECT_NE(seg_id, topo_id);

  auto seg_id_copy = icts::PatternId::segment(support::kPatternId42);
  EXPECT_EQ(seg_id, seg_id_copy);
}

TEST(PatternIdTest, PackUnique)
{
  auto seg_id = icts::PatternId::segment(support::kPatternId42);
  auto topo_id = icts::PatternId::topology(support::kPatternId42);

  EXPECT_NE(seg_id.pack(), topo_id.pack());
}

TEST(HashJoinEngineTest, PackFunction)
{
  unsigned key = icts::detail::Pack(support::kPackHigh1234, support::kPackLow5678);
  EXPECT_EQ(key, support::kPackedKey12345678);

  key = icts::detail::Pack(support::kPackWordFFFF, support::kPackWordZero);
  EXPECT_EQ(key, support::kPackedKeyFFFF0000);

  key = icts::detail::Pack(support::kPackWordZero, support::kPackWordFFFF);
  EXPECT_EQ(key, support::kPackedKey0000FFFF);
}

}  // namespace
}  // namespace icts_test
