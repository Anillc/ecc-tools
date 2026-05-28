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
#include "Comparator.hh"

#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "CompareMath.hh"

namespace ircx {
namespace compare_spef {
namespace {

auto hasExplicitCompareMode(const Config& config) -> bool
{
  return config.compare_capacitance || config.compare_resistance || config.compare_delay;
}

auto shouldCompareCapacitance(const Config& config) -> bool
{
  return hasExplicitCompareMode(config) ? config.compare_capacitance : true;
}

auto shouldCompareResistance(const Config& config) -> bool
{
  return hasExplicitCompareMode(config) ? config.compare_resistance : true;
}

}  // namespace

Comparator::Comparator(const Config& config)
    : _config(config),
      _net_selector(config),
      _coupling_cap_comparator(config),
      _path_pair_generator(config),
      _compare_capacitance(shouldCompareCapacitance(config)),
      _compare_resistance(shouldCompareResistance(config))
{
}

auto Comparator::compare(const Data& test, const Data& reference) -> Result
{
  Result result;
  initializeSummary(test, reference, result);
  compareMatchedNets(test, reference, result);
  collectTestOnlyNets(test, reference, result);
  if (_compare_capacitance) {
    _coupling_cap_comparator.compare(test, reference, result);
  }
  finishSummary(test, reference, result);
  return result;
}

void Comparator::initializeSummary(const Data& test, const Data& reference, Result& result) const
{
  result.summary.reference_net_count = reference.nets.size();
  result.summary.test_net_count = test.nets.size();
  result.summary.reference_coupling_count = reference.coupling_caps.size();
  result.summary.test_coupling_count = test.coupling_caps.size();
}

void Comparator::compareMatchedNets(const Data& test, const Data& reference, Result& result) const
{
  for (const auto& [net_name, reference_net] : reference.nets) {
    const auto test_it = test.nets.find(net_name);
    if (test_it == test.nets.end()) {
      result.reference_only_nets.push_back(net_name);
      continue;
    }

    compareMatchedNet(net_name, reference_net, test_it->second, result);
  }
}

void Comparator::compareMatchedNet(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
{
  result.summary.matched_net_count++;
  if (!_net_selector.selected(reference_net)) {
    return;
  }

  if (_compare_capacitance && reference_net.total_cap >= _config.tcap_threshold) {
    addTotalCapRow(net_name, reference_net, test_net, result);
  }

  if (_compare_resistance) {
    addResistanceRows(net_name, reference_net, test_net, result);
  }
}

void Comparator::addTotalCapRow(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
{
  ValueRow row;
  row.net = net_name;
  row.reference = reference_net.total_cap;
  row.test = test_net.total_cap;
  row.delta = row.test - row.reference;
  row.relative_delta = math::capacitanceRelativeDelta(row.test, row.reference);
  result.tcap_rows.push_back(std::move(row));
}

void Comparator::addResistanceRows(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
{
  for (const auto& pair : _path_pair_generator.generate(reference_net)) {
    const auto reference_res = _resistance_solver.equivalentResistance(reference_net, pair.first, pair.second);
    if (!reference_res.has_value() || *reference_res < _config.res_threshold) {
      continue;
    }
    const auto test_res = _resistance_solver.equivalentResistance(test_net, pair.first, pair.second);

    ResistanceRow row;
    row.net = net_name;
    row.from_pin = pair.first;
    row.to_pin = pair.second;
    row.reference_valid = true;
    row.reference = *reference_res;
    row.test_valid = test_res.has_value();
    row.test = test_res.value_or(std::numeric_limits<double>::quiet_NaN());
    row.delta = test_res.has_value() ? *test_res - *reference_res : std::numeric_limits<double>::quiet_NaN();
    row.relative_delta = test_res.has_value() ? math::absoluteRelativeDelta(*test_res, *reference_res) : std::nullopt;
    result.p2p_rows.push_back(std::move(row));
  }
}

void Comparator::collectTestOnlyNets(const Data& test, const Data& reference, Result& result) const
{
  for (const auto& net_pair : test.nets) {
    const std::string& net_name = net_pair.first;
    if (!reference.nets.contains(net_name)) {
      result.test_only_nets.push_back(net_name);
    }
  }
}

void Comparator::finishSummary(const Data& test, const Data& reference, Result& result) const
{
  result.summary.reference_only_net_count = result.reference_only_nets.size();
  result.summary.test_only_net_count = result.test_only_nets.size();
  result.summary.reference_only_coupling_count = result.reference_only_couplings.size();
  result.summary.test_only_coupling_count = result.test_only_couplings.size();
  result.summary.tcap_row_count = result.tcap_rows.size();
  result.summary.ccap_row_count = result.ccap_rows.size();
  result.summary.p2p_row_count = result.p2p_rows.size();
  _result_sorter.sort(result, test, reference);
}

}  // namespace compare_spef
}  // namespace ircx
