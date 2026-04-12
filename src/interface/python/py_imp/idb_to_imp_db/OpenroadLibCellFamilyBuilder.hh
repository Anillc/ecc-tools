#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Lib.hh"

namespace python_interface {

struct OpenroadLibCellFamily
{
  std::string representative_name;
  std::vector<ista::LibCell*> members;
};

struct OpenroadLibCellFamilyBuildResult
{
  std::vector<OpenroadLibCellFamily> families;
  std::unordered_map<std::string, std::string> cell_name_to_representative;
};

namespace openroad_libcell_family_builder {

template <typename T>
inline std::size_t hash_value(const T& value)
{
  return std::hash<T>{}(value);
}

inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs)
{
  return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6) + (lhs >> 2));
}

inline std::size_t hash_port(ista::LibPort* port)
{
  std::size_t hash = 0;
  hash = hash_combine(hash, hash_value(std::string(port->get_port_name())));
  hash = hash_combine(hash, hash_value(static_cast<int>(port->get_port_type())));
  hash = hash_combine(hash, hash_value(static_cast<int>(port->get_is_clock_pin())));
  hash = hash_combine(hash, hash_value(static_cast<int>(port->get_clock_gate_clock_pin())));
  hash = hash_combine(hash, hash_value(static_cast<int>(port->get_clock_gate_enable_pin())));
  return hash;
}

inline bool compare_port(ista::LibPort* lhs, ista::LibPort* rhs)
{
  return (lhs == nullptr && rhs == nullptr)
         || (lhs != nullptr && rhs != nullptr && std::string(lhs->get_port_name()) == std::string(rhs->get_port_name())
             && lhs->get_port_type() == rhs->get_port_type() && lhs->get_is_clock_pin() == rhs->get_is_clock_pin()
             && lhs->get_clock_gate_clock_pin() == rhs->get_clock_gate_clock_pin()
             && lhs->get_clock_gate_enable_pin() == rhs->get_clock_gate_enable_pin());
}

inline std::size_t hash_func_expr(RustLibertyExpr* expr)
{
  if (expr == nullptr) {
    return 0;
  }

  switch (expr->op) {
    case RustLibertyExprOp::kBuffer:
      return hash_value(std::string(expr->port_name));
    case RustLibertyExprOp::kNot: {
      auto* left_expr = rust_get_expr_left(expr);
      auto result = hash_func_expr(left_expr);
      rust_free_expr(left_expr);
      return hash_combine(hash_value(static_cast<unsigned>(expr->op)), result);
    }
    default: {
      auto* left_expr = rust_get_expr_left(expr);
      auto* right_expr = rust_get_expr_right(expr);
      auto left_hash = hash_func_expr(left_expr);
      auto right_hash = hash_func_expr(right_expr);
      rust_free_expr(left_expr);
      rust_free_expr(right_expr);
      return hash_combine(hash_value(static_cast<unsigned>(expr->op)), hash_combine(left_hash, right_hash));
    }
  }
}

inline bool compare_func_expr(RustLibertyExpr* lhs, RustLibertyExpr* rhs)
{
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  if (lhs->op != rhs->op) {
    return false;
  }

  switch (lhs->op) {
    case RustLibertyExprOp::kBuffer:
      return std::string(lhs->port_name) == std::string(rhs->port_name);
    case RustLibertyExprOp::kNot: {
      auto* lhs_left = rust_get_expr_left(lhs);
      auto* rhs_left = rust_get_expr_left(rhs);
      bool result = compare_func_expr(lhs_left, rhs_left);
      rust_free_expr(lhs_left);
      rust_free_expr(rhs_left);
      return result;
    }
    default: {
      auto* lhs_left = rust_get_expr_left(lhs);
      auto* lhs_right = rust_get_expr_right(lhs);
      auto* rhs_left_expr = rust_get_expr_left(rhs);
      auto* rhs_right_expr = rust_get_expr_right(rhs);
      bool result = compare_func_expr(lhs_left, rhs_left_expr) && compare_func_expr(lhs_right, rhs_right_expr);
      rust_free_expr(lhs_left);
      rust_free_expr(lhs_right);
      rust_free_expr(rhs_left_expr);
      rust_free_expr(rhs_right_expr);
      return result;
    }
  }
}

inline std::size_t hash_cell_ports(ista::LibCell* cell)
{
  std::size_t hash = 0;
  for (const auto& port : cell->get_cell_ports()) {
    hash = hash_combine(hash, hash_port(port.get()));
    hash = hash_combine(hash, hash_func_expr(port->get_func_expr()));
  }
  for (const auto& bus : cell->get_cell_buses()) {
    hash = hash_combine(hash, hash_port(bus.get()));
    hash = hash_combine(hash, hash_func_expr(bus->get_func_expr()));
  }
  return hash;
}

inline bool compare_cell_ports(ista::LibCell* lhs, ista::LibCell* rhs)
{
  const auto lhs_port_count = lhs->get_cell_ports().size() + lhs->get_cell_buses().size();
  const auto rhs_port_count = rhs->get_cell_ports().size() + rhs->get_cell_buses().size();
  if (lhs_port_count != rhs_port_count) {
    return false;
  }

  for (const auto& port : lhs->get_cell_ports()) {
    auto* rhs_port = rhs->get_cell_port_or_port_bus(port->get_port_name());
    if (!(rhs_port && compare_port(port.get(), rhs_port))) {
      return false;
    }
  }
  for (const auto& bus : lhs->get_cell_buses()) {
    auto* rhs_bus = rhs->get_cell_port_or_port_bus(bus->get_port_name());
    if (!(rhs_bus && compare_port(bus.get(), rhs_bus))) {
      return false;
    }
  }
  return true;
}

inline bool cell_has_funcs(ista::LibCell* cell)
{
  for (const auto& port : cell->get_cell_ports()) {
    if (port->get_func_expr() != nullptr) {
      return true;
    }
  }
  for (const auto& bus : cell->get_cell_buses()) {
    if (bus->get_func_expr() != nullptr) {
      return true;
    }
  }
  return false;
}

inline bool compare_cell_funcs(ista::LibCell* lhs, ista::LibCell* rhs)
{
  for (const auto& port : lhs->get_cell_ports()) {
    auto* rhs_port = rhs->get_cell_port_or_port_bus(port->get_port_name());
    if (!(rhs_port && compare_func_expr(port->get_func_expr(), rhs_port->get_func_expr()))) {
      return false;
    }
  }
  for (const auto& bus : lhs->get_cell_buses()) {
    auto* rhs_bus = rhs->get_cell_port_or_port_bus(bus->get_port_name());
    if (!(rhs_bus && compare_func_expr(bus->get_func_expr(), rhs_bus->get_func_expr()))) {
      return false;
    }
  }
  return true;
}

inline std::size_t hash_arc_signature(ista::LibArcSet* arc_set)
{
  auto* arc = arc_set->front();
  std::size_t hash = 0;
  hash = hash_combine(hash, hash_value(std::string(arc->get_src_port())));
  hash = hash_combine(hash, hash_value(std::string(arc->get_snk_port())));
  hash = hash_combine(hash, hash_value(static_cast<int>(arc->get_timing_type())));
  hash = hash_combine(hash, hash_value(static_cast<int>(arc->get_timing_sense())));
  return hash;
}

inline std::size_t hash_cell_sequential_like_features(ista::LibCell* cell)
{
  std::size_t hash = 0;
  hash = hash_combine(hash, hash_value(static_cast<int>(cell->isSequentialCell())));
  hash = hash_combine(hash, hash_value(static_cast<int>(cell->isICG())));
  for (const auto& arc_set : cell->get_cell_arcs()) {
    hash = hash_combine(hash, hash_arc_signature(arc_set.get()));
  }
  return hash;
}

inline bool compare_timing_arc(ista::LibArcSet* lhs, ista::LibArcSet* rhs)
{
  auto* lhs_arc = lhs->front();
  auto* rhs_arc = rhs->front();
  return std::string(lhs_arc->get_src_port()) == std::string(rhs_arc->get_src_port())
         && std::string(lhs_arc->get_snk_port()) == std::string(rhs_arc->get_snk_port())
         && lhs_arc->get_timing_type() == rhs_arc->get_timing_type()
         && lhs_arc->get_timing_sense() == rhs_arc->get_timing_sense();
}

inline bool compare_timing_arc_sets(ista::LibCell* lhs, ista::LibCell* rhs)
{
  if (lhs->get_cell_arcs().size() != rhs->get_cell_arcs().size()) {
    return false;
  }

  for (const auto& lhs_arc_set : lhs->get_cell_arcs()) {
    auto rhs_arc_set = rhs->findLibertyArcSet(lhs_arc_set->front()->get_src_port(), lhs_arc_set->front()->get_snk_port(),
                                              lhs_arc_set->front()->get_timing_type());
    if (!(rhs_arc_set && compare_timing_arc(lhs_arc_set.get(), *rhs_arc_set))) {
      return false;
    }
  }
  return true;
}

inline std::size_t hash_cell(ista::LibCell* cell)
{
  std::size_t hash = 0;
  hash = hash_combine(hash, hash_cell_ports(cell));
  hash = hash_combine(hash, hash_cell_sequential_like_features(cell));
  return hash;
}

inline bool equiv_cells(ista::LibCell* lhs, ista::LibCell* rhs)
{
  if (!compare_cell_ports(lhs, rhs)) {
    return false;
  }
  if (!compare_cell_funcs(lhs, rhs)) {
    return false;
  }
  if (lhs->isSequentialCell() != rhs->isSequentialCell()) {
    return false;
  }
  if (lhs->isICG() != rhs->isICG()) {
    return false;
  }
  if (cell_has_funcs(lhs)) {
    return true;
  }
  return compare_timing_arc_sets(lhs, rhs);
}

inline double cell_drive_resistance(ista::LibCell* cell)
{
  for (const auto& port : cell->get_cell_ports()) {
    if (port->isOutput()) {
      return port->driveResistance();
    }
  }
  for (const auto& bus : cell->get_cell_buses()) {
    if (bus->isOutput()) {
      return bus->driveResistance();
    }
  }
  return 0.0;
}

inline OpenroadLibCellFamilyBuildResult build(const std::vector<ista::LibLibrary*>& libs)
{
  OpenroadLibCellFamilyBuildResult result;
  std::unordered_map<std::size_t, std::vector<ista::LibCell*>> hash_matches;
  std::unordered_map<ista::LibCell*, ista::LibCell*> cell_to_anchor;
  std::unordered_map<ista::LibCell*, std::size_t> anchor_to_family_index;

  for (auto* lib : libs) {
    if (lib == nullptr) {
      continue;
    }
    for (const auto& cell_uptr : lib->get_cells()) {
      auto* cell = cell_uptr.get();
      if (cell == nullptr || cell->isDontUse()) {
        continue;
      }

      auto& bucket = hash_matches[hash_cell(cell)];
      ista::LibCell* matched_anchor = nullptr;
      for (auto* match : bucket) {
        if (equiv_cells(match, cell)) {
          matched_anchor = cell_to_anchor.at(match);
          break;
        }
      }

      if (matched_anchor == nullptr) {
        matched_anchor = cell;
        anchor_to_family_index[matched_anchor] = result.families.size();
        result.families.push_back(OpenroadLibCellFamily{matched_anchor->get_cell_name(), {cell}});
      } else {
        result.families.at(anchor_to_family_index.at(matched_anchor)).members.push_back(cell);
      }

      cell_to_anchor[cell] = matched_anchor;
      result.cell_name_to_representative[cell->get_cell_name()] = matched_anchor->get_cell_name();
      bucket.push_back(cell);
    }
  }

  for (auto& family : result.families) {
    std::stable_sort(family.members.begin(), family.members.end(), [](ista::LibCell* lhs, ista::LibCell* rhs) {
      return cell_drive_resistance(lhs) > cell_drive_resistance(rhs);
    });
  }

  return result;
}

}  // namespace openroad_libcell_family_builder
}  // namespace python_interface
