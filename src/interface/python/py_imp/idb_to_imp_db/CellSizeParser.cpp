#include "idb_to_imp_db/CellSizeParser.hh"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <tuple>

namespace python_interface::cell_size {
namespace {

std::string to_upper(std::string_view input)
{
  std::string output(input);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return output;
}

std::string to_lower(std::string_view input)
{
  std::string output(input);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return output;
}

bool is_asap7_process_node(std::string_view process_node)
{
  const std::string lowered = to_lower(process_node);
  return lowered.find("asap7") != std::string::npos;
}

bool looks_like_asap7_cell(std::string_view cell_name)
{
  const std::string upper_name = to_upper(cell_name);
  if (upper_name.find("ASAP7") != std::string::npos) {
    return true;
  }

  static const std::regex re_asap7_fractional("X(P\\d+|\\d+P\\d+)");
  return std::regex_search(upper_name, re_asap7_fractional);
}

std::optional<double> parse_numeric_token(std::string token)
{
  if (token.empty()) {
    return std::nullopt;
  }

  if (token.front() == 'P') {
    token = "0." + token.substr(1);
  } else {
    std::replace(token.begin(), token.end(), 'P', '.');
  }

  try {
    return std::stod(token);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<double> try_parse_x_drive(std::string_view cell_name, bool allow_leading_fraction)
{
  const std::string upper_name = to_upper(cell_name);
  static const std::regex re_standard_x("X(\\d+(?:[P\\.]\\d+)?)");
  static const std::regex re_fractional_x("X(P\\d+|\\d+(?:[P\\.]\\d+)?)");

  const std::regex& pattern = allow_leading_fraction ? re_fractional_x : re_standard_x;
  std::smatch match;
  if (!std::regex_search(upper_name, match, pattern)) {
    return std::nullopt;
  }
  return parse_numeric_token(match[1].str());
}

std::optional<double> try_parse_d_drive(std::string_view cell_name)
{
  const std::string upper_name = to_upper(cell_name);
  if (upper_name.find("BUF") == std::string::npos && upper_name.find("INV") == std::string::npos) {
    return std::nullopt;
  }

  static const std::regex re_d("D(\\d+(?:[P\\.]\\d+)?)");
  std::smatch match;
  if (!std::regex_search(upper_name, match, re_d)) {
    return std::nullopt;
  }
  return parse_numeric_token(match[1].str());
}

std::optional<double> parse_by_kind(std::string_view cell_name, ParserKind parser_kind)
{
  if (parser_kind == ParserKind::kAsap7) {
    if (auto parsed = try_parse_x_drive(cell_name, true); parsed.has_value()) {
      return parsed;
    }
  } else if (auto parsed = try_parse_x_drive(cell_name, false); parsed.has_value()) {
    return parsed;
  }

  if (auto parsed = try_parse_d_drive(cell_name); parsed.has_value()) {
    return parsed;
  }

  if (parser_kind != ParserKind::kAsap7) {
    return std::nullopt;
  }

  return try_parse_x_drive(cell_name, false);
}

}  // namespace

bool CellSortKey::operator<(const CellSortKey& other) const
{
  return std::tie(size, leakage_mw, cell_name) < std::tie(other.size, other.leakage_mw, other.cell_name);
}

ParserKind detect_parser_kind(std::string_view cell_name, std::string_view process_node)
{
  if (!process_node.empty()) {
    return is_asap7_process_node(process_node) ? ParserKind::kAsap7 : ParserKind::kGeneric;
  }
  return looks_like_asap7_cell(cell_name) ? ParserKind::kAsap7 : ParserKind::kGeneric;
}

double parse_cell_size_width(std::string_view cell_name, std::string_view process_node)
{
  const ParserKind parser_kind = detect_parser_kind(cell_name, process_node);
  if (auto parsed = parse_by_kind(cell_name, parser_kind); parsed.has_value()) {
    return *parsed;
  }

  const ParserKind fallback_kind = parser_kind == ParserKind::kAsap7 ? ParserKind::kGeneric : ParserKind::kAsap7;
  if (auto parsed = parse_by_kind(cell_name, fallback_kind); parsed.has_value()) {
    return *parsed;
  }

  return 1.0;
}

CellSortKey build_cell_sort_key(std::string_view cell_name, double leakage_mw, std::string_view process_node)
{
  return CellSortKey{
      .size = parse_cell_size_width(cell_name, process_node),
      .leakage_mw = leakage_mw,
      .cell_name = std::string(cell_name),
  };
}

}  // namespace python_interface::cell_size
