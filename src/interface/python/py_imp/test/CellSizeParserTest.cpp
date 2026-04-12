#include "gtest/gtest.h"

#include "idb_to_imp_db/CellSizeParser.hh"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

namespace python_interface::cell_size {
namespace {

TEST(CellSizeParserTest, ParsesGenericDrivePatterns)
{
  EXPECT_DOUBLE_EQ(parse_cell_size_width("NAND2X0P5H7R"), 0.5);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("AOI221X1P4H7L"), 1.4);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("INV_X16"), 16.0);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("BUFFD4BWP30P140LVT"), 4.0);
}

TEST(CellSizeParserTest, ParsesAsap7FractionalPatterns)
{
  EXPECT_DOUBLE_EQ(parse_cell_size_width("INVxp33_ASAP7_75t_R"), 0.33);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("INVxp67_ASAP7_75t_R"), 0.67);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("NOR4xp25_ASAP7_75t_SL"), 0.25);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("NOR4xp75_ASAP7_75t_SL"), 0.75);
  EXPECT_DOUBLE_EQ(parse_cell_size_width("NAND2x1p5_ASAP7_75t_R"), 1.5);
}

TEST(CellSizeParserTest, UsesSizeAsPrimarySortKeyBeforeLeakage)
{
  std::vector<CellSortKey> keys = {
      build_cell_sort_key("INVx1_ASAP7_75t_R", 1.0),
      build_cell_sort_key("INVxp67_ASAP7_75t_R", 100.0),
      build_cell_sort_key("INVxp33_ASAP7_75t_R", 10.0),
  };

  std::sort(keys.begin(), keys.end());

  ASSERT_EQ(keys.size(), 3U);
  EXPECT_EQ(keys[0].cell_name, "INVxp33_ASAP7_75t_R");
  EXPECT_EQ(keys[1].cell_name, "INVxp67_ASAP7_75t_R");
  EXPECT_EQ(keys[2].cell_name, "INVx1_ASAP7_75t_R");
}

TEST(CellSizeParserTest, UsesLeakageAsSecondarySortKeyWhenSizeMatches)
{
  std::vector<CellSortKey> keys = {
      build_cell_sort_key("NAND2X1H7R", 3.0),
      build_cell_sort_key("NAND2X1H7L", 1.0),
      build_cell_sort_key("NAND2X1H7H", 2.0),
  };

  std::sort(keys.begin(), keys.end());

  ASSERT_EQ(keys.size(), 3U);
  EXPECT_EQ(keys[0].cell_name, "NAND2X1H7L");
  EXPECT_EQ(keys[1].cell_name, "NAND2X1H7H");
  EXPECT_EQ(keys[2].cell_name, "NAND2X1H7R");
}

}  // namespace
}  // namespace python_interface::cell_size
