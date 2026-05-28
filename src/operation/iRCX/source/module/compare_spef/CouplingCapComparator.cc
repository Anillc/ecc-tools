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
#include "CouplingCapComparator.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include "CompareMath.hh"

namespace ircx {
namespace compare_spef {
namespace {

auto isExternalNet(const Data& data, const std::string& net_name) -> bool
{
  const auto net_it = data.nets.find(net_name);
  if (net_it == data.nets.end()) {
    return false;
  }
  return std::any_of(net_it->second.pins.begin(), net_it->second.pins.end(), [](const Pin& pin) { return pin.is_external; });
}

auto netOrder(const Data& data, const std::string& net_name) -> std::size_t
{
  const auto order_it = data.net_order.find(net_name);
  if (order_it == data.net_order.end()) {
    return 0;
  }
  return order_it->second;
}

auto makeReportCouplingPair(const Data& data, const NodePair& key) -> NodePair
{
  const bool first_external = isExternalNet(data, key.first);
  const bool second_external = isExternalNet(data, key.second);
  if (first_external != second_external) {
    return first_external ? key : NodePair{key.second, key.first};
  }

  const auto first_order_it = data.net_order.find(key.first);
  const auto second_order_it = data.net_order.find(key.second);
  if (first_order_it == data.net_order.end() || second_order_it == data.net_order.end()) {
    return key;
  }

  if (first_external && second_external && first_order_it->second < second_order_it->second) {
    return NodePair{key.second, key.first};
  }
  return key;
}

auto makeCcapMismatch(const Data& data, const NodePair& key, double capacitance) -> CcapMismatch
{
  CcapMismatch mismatch;
  mismatch.nets = key;
  mismatch.report_nets = makeReportCouplingPair(data, key);
  mismatch.first_order = netOrder(data, mismatch.report_nets.first);
  mismatch.second_order = netOrder(data, mismatch.report_nets.second);
  mismatch.first_external = isExternalNet(data, mismatch.report_nets.first);
  mismatch.second_external = isExternalNet(data, mismatch.report_nets.second);
  mismatch.capacitance = capacitance;
  return mismatch;
}

}  // namespace

CouplingCapComparator::CouplingCapComparator(const Config& config) : _config(config), _net_selector(config)
{
}

void CouplingCapComparator::compare(const Data& test, const Data& reference, Result& result) const
{
  for (const auto& [key, reference_cap] : reference.coupling_caps) {
    const auto test_cap_it = test.coupling_caps.find(key);
    if (test_cap_it == test.coupling_caps.end()) {
      result.reference_only_couplings.push_back(makeCcapMismatch(reference, key, reference_cap));
      continue;
    }

    addRow(reference, key.first, key.second, reference_cap, test_cap_it->second, result);
    addRow(reference, key.second, key.first, reference_cap, test_cap_it->second, result);
  }

  for (const auto& [key, test_cap] : test.coupling_caps) {
    if (!reference.coupling_caps.contains(key)) {
      result.test_only_couplings.push_back(makeCcapMismatch(test, key, test_cap));
    }
  }
}

void CouplingCapComparator::addRow(const Data& reference, const std::string& victim, const std::string& aggressor, double reference_cap,
                                   double test_cap, Result& result) const
{
  const auto reference_victim_it = reference.nets.find(victim);
  if (reference_victim_it == reference.nets.end() || !_net_selector.selected(reference_victim_it->second)) {
    return;
  }

  const double reference_victim_tcap = reference_victim_it->second.total_cap;
  const double reference_rel = reference_victim_tcap <= math::kEpsilon ? 0.0 : std::abs(reference_cap) / reference_victim_tcap;
  if (std::abs(reference_cap) < _config.ccap_abs_threshold || reference_rel < _config.ccap_rel_threshold) {
    return;
  }

  CcapRow row;
  row.victim = victim;
  row.aggressor = aggressor;
  row.reference = reference_cap;
  row.test = test_cap;
  row.delta = row.test - row.reference;
  row.reference_victim_total_cap = reference_victim_tcap;
  row.relative_delta = math::couplingRelativeDelta(row.test, row.reference, row.reference_victim_total_cap);
  result.ccap_rows.push_back(std::move(row));
}

}  // namespace compare_spef
}  // namespace ircx
