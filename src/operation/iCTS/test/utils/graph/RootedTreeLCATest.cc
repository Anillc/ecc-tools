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
 * @file RootedTreeLCATest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Unit tests for rooted-tree LCA helper.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "graph/RootedTreeLCA.hh"

namespace icts_test {
namespace {

using icts::graph::RootedTreeLCA;

TEST(RootedTreeLCATest, FindsLowestCommonAncestor)
{
  const std::vector<std::size_t> parents{
      RootedTreeLCA::kInvalidNode, 0U, 0U, 1U, 1U, 2U, 2U, 4U,
  };

  const RootedTreeLCA lca(parents);

  EXPECT_TRUE(lca.isValid());
  EXPECT_EQ(lca.lca(3U, 7U), 1U);
  EXPECT_EQ(lca.lca(5U, 6U), 2U);
  EXPECT_EQ(lca.lca(3U, 6U), 0U);
  EXPECT_EQ(lca.lca(7U, 7U), 7U);
}

TEST(RootedTreeLCATest, ExtractsAncestorPath)
{
  const std::vector<std::size_t> parents{
      RootedTreeLCA::kInvalidNode, 0U, 0U, 1U, 1U, 4U,
  };

  const RootedTreeLCA lca(parents);

  EXPECT_EQ(lca.ancestorPath(1U, 5U), (std::vector<std::size_t>{1U, 4U, 5U}));
  EXPECT_EQ(lca.ancestorPath(1U, 5U, false, true), (std::vector<std::size_t>{4U, 5U}));
  EXPECT_EQ(lca.ancestorPath(1U, 5U, false, false), (std::vector<std::size_t>{4U}));
  EXPECT_TRUE(lca.ancestorPath(2U, 5U).empty());
}

TEST(RootedTreeLCATest, RejectsInvalidParentVector)
{
  EXPECT_FALSE(RootedTreeLCA(std::vector<std::size_t>{RootedTreeLCA::kInvalidNode, RootedTreeLCA::kInvalidNode}).isValid());
  EXPECT_FALSE(RootedTreeLCA(std::vector<std::size_t>{1U, 0U}).isValid());
  EXPECT_FALSE(RootedTreeLCA(std::vector<std::size_t>{RootedTreeLCA::kInvalidNode, 4U}).isValid());
}

}  // namespace
}  // namespace icts_test
