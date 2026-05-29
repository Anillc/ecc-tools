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
#include "compare/CouplingCapComparator.hh"

#include <omp.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "compare/CompareMath.hh"
#include "compare/CompareParallel.hh"

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

void appendRows(Result& result, Result&& thread_result)
{
  result.ccap_rows.insert(result.ccap_rows.end(), std::make_move_iterator(thread_result.ccap_rows.begin()),
                          std::make_move_iterator(thread_result.ccap_rows.end()));
  result.reference_only_couplings.insert(result.reference_only_couplings.end(),
                                         std::make_move_iterator(thread_result.reference_only_couplings.begin()),
                                         std::make_move_iterator(thread_result.reference_only_couplings.end()));
  result.test_only_couplings.insert(result.test_only_couplings.end(), std::make_move_iterator(thread_result.test_only_couplings.begin()),
                                    std::make_move_iterator(thread_result.test_only_couplings.end()));
}

}  // namespace

CouplingCapComparator::CouplingCapComparator(const Config& config) : _config(config), _net_selector(config)
{
}

void CouplingCapComparator::compare(const Data& test, const Data& reference, Result& result) const
{
  const int reference_thread_count = parallel::threadCount(_config, reference.coupling_caps.size());
  std::vector<const std::map<NodePair, double>::value_type*> reference_couplings;
  reference_couplings.reserve(reference.coupling_caps.size());
  for (const auto& reference_coupling : reference.coupling_caps) {
    reference_couplings.push_back(&reference_coupling);
  }

  std::vector<Result> reference_thread_results(reference_thread_count);
#pragma omp parallel for schedule(dynamic, 256) num_threads(reference_thread_count)
  for (std::size_t index = 0; index < reference_couplings.size(); ++index) {
    const auto& [key, reference_cap] = *reference_couplings[index];
    const auto test_cap_it = test.coupling_caps.find(key);
    if (test_cap_it == test.coupling_caps.end()) {
      reference_thread_results[omp_get_thread_num()].reference_only_couplings.push_back(makeCcapMismatch(reference, key, reference_cap));
      continue;
    }

    addRow(reference, key.first, key.second, reference_cap, test_cap_it->second, reference_thread_results[omp_get_thread_num()]);
    addRow(reference, key.second, key.first, reference_cap, test_cap_it->second, reference_thread_results[omp_get_thread_num()]);
  }

  for (auto& thread_result : reference_thread_results) {
    appendRows(result, std::move(thread_result));
  }

  const int test_thread_count = parallel::threadCount(_config, test.coupling_caps.size());
  std::vector<const std::map<NodePair, double>::value_type*> test_couplings;
  test_couplings.reserve(test.coupling_caps.size());
  for (const auto& test_coupling : test.coupling_caps) {
    test_couplings.push_back(&test_coupling);
  }

  std::vector<Result> test_thread_results(test_thread_count);
#pragma omp parallel for schedule(dynamic, 256) num_threads(test_thread_count)
  for (std::size_t index = 0; index < test_couplings.size(); ++index) {
    const auto& [key, test_cap] = *test_couplings[index];
    if (!reference.coupling_caps.contains(key)) {
      test_thread_results[omp_get_thread_num()].test_only_couplings.push_back(makeCcapMismatch(test, key, test_cap));
    }
  }

  for (auto& thread_result : test_thread_results) {
    appendRows(result, std::move(thread_result));
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
