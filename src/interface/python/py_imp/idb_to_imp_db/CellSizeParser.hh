#pragma once

#include <string>
#include <string_view>

namespace python_interface::cell_size {

enum class ParserKind
{
  kGeneric,
  kAsap7,
};

struct CellSortKey
{
  double size = 1.0;
  double leakage_mw = 0.0;
  std::string cell_name;

  bool operator<(const CellSortKey& other) const;
};

ParserKind detect_parser_kind(std::string_view cell_name, std::string_view process_node = "");
double parse_cell_size_width(std::string_view cell_name, std::string_view process_node = "");
CellSortKey build_cell_sort_key(std::string_view cell_name, double leakage_mw, std::string_view process_node = "");

}  // namespace python_interface::cell_size
