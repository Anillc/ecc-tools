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
#include "compare/PathPairGenerator.hh"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "StringUtils.hh"

namespace ircx {
namespace compare_spef {
namespace {

constexpr std::size_t kMaxPathPairs = 250;
constexpr std::size_t kMaxDefaultSinksPerSource = 5;

struct PinCounts
{
  std::size_t sources = 0;
  std::size_t sinks = 0;
};

auto isSourcePin(const Pin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "I";
  }
  return pin.direction == "O";
}

auto isSinkPin(const Pin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "O";
  }
  return pin.direction == "I";
}

auto isRegisterDataSinkName(std::string_view pin_name) -> bool
{
  return pin_name != "cs_reg_p:D" && string::contains(pin_name, "_reg_p:D");
}

auto isNamedRegisterSource(std::string_view pin_name) -> bool
{
  return string::starts_with(pin_name, "cs_reg_p:") || string::starts_with(pin_name, "cvtA_reg_p:");
}

auto isTopOutputBufferNet(const Net& net, std::string_view sink_name) -> bool
{
  return string::contains(sink_name, "fixio_buf_")
         && std::any_of(net.pins.begin(), net.pins.end(), [](const Pin& pin) { return pin.is_external && pin.direction == "O"; });
}

auto sourceShouldPrecedeNamedLoad(std::string_view source_name, std::string_view sink_name) -> bool
{
  return string::starts_with(source_name, "fixio_buf_") && string::starts_with(sink_name, "max_cap");
}

auto isTieCell(const Pin& pin) -> bool
{
  return string::starts_with(pin.driving_cell, "TIE");
}

auto p2pReportPair(const Net& net, const std::string& source_name, const Pin& source_pin, const std::string& sink_name) -> NodePair
{
  const bool source_first = source_pin.is_external || isRegisterDataSinkName(sink_name) || isTieCell(source_pin)
                            || isNamedRegisterSource(source_name) || isTopOutputBufferNet(net, sink_name)
                            || sourceShouldPrecedeNamedLoad(source_name, sink_name);
  if (source_first) {
    return NodePair{source_name, sink_name};
  }
  return NodePair{sink_name, source_name};
}

auto pinNames(const Net& net) -> std::vector<std::string>
{
  std::vector<std::string> names;
  names.reserve(net.pins.size());
  for (const auto& pin : net.pins) {
    if (pin.direction == "N") {
      continue;
    }
    names.push_back(pin.name);
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

auto pinMap(const Net& net) -> std::map<std::string, Pin>
{
  std::map<std::string, Pin> pins;
  for (const auto& pin : net.pins) {
    if (pin.direction == "N" || pin.name.empty()) {
      continue;
    }
    pins.try_emplace(pin.name, pin);
  }
  return pins;
}

auto countPins(const std::map<std::string, Pin>& pins) -> PinCounts
{
  PinCounts counts;
  for (const auto& pin_pair : pins) {
    const Pin& pin = pin_pair.second;
    if (isSourcePin(pin)) {
      counts.sources++;
    }
    if (isSinkPin(pin)) {
      counts.sinks++;
    }
  }
  return counts;
}

void addPathPair(std::set<NodePair>& pairs, const std::string& from_pin, const std::string& to_pin)
{
  if (!from_pin.empty() && !to_pin.empty() && from_pin != to_pin) {
    pairs.insert(NodePair{from_pin, to_pin});
  }
}

auto shouldStopAddingSinks(std::string_view source_name, const PinCounts& counts, std::size_t sink_count) -> bool
{
  const bool is_small_register_output_net = string::contains(source_name, "_reg_p:Q") && counts.sinks <= kMaxDefaultSinksPerSource + 3;
  const bool should_limit = counts.sinks > kMaxDefaultSinksPerSource && (counts.sources > 1 || is_small_register_output_net);
  return should_limit && sink_count >= kMaxDefaultSinksPerSource;
}

auto appendSourceSinkPairs(const Net& net, const std::map<std::string, Pin>& pins, const PinCounts& counts, std::set<NodePair>& pair_set,
                           std::vector<NodePair>& pairs) -> bool
{
  for (const auto& [source_name, source_pin] : pins) {
    if (!isSourcePin(source_pin)) {
      continue;
    }
    std::size_t sink_count = 0;
    for (const auto& [sink_name, sink_pin] : pins) {
      if (source_name == sink_name || !isSinkPin(sink_pin)) {
        continue;
      }

      NodePair report_pair = p2pReportPair(net, source_name, source_pin, sink_name);
      if (pair_set.insert(report_pair).second) {
        pairs.push_back(std::move(report_pair));
        sink_count++;
        if (pairs.size() >= kMaxPathPairs) {
          return true;
        }
        if (shouldStopAddingSinks(source_name, counts, sink_count)) {
          break;
        }
      }
    }
  }
  return false;
}

auto appendExternalInternalPairs(const std::map<std::string, Pin>& pins, std::set<NodePair>& pair_set, std::vector<NodePair>& pairs) -> bool
{
  for (const auto& [external_name, external_pin] : pins) {
    if (!external_pin.is_external || external_pin.direction == "N") {
      continue;
    }
    for (const auto& [internal_name, internal_pin] : pins) {
      if (internal_pin.is_external || external_name == internal_name || internal_pin.direction == "N"
          || external_pin.direction == internal_pin.direction) {
        continue;
      }
      NodePair report_pair{external_name, internal_name};
      if (pair_set.insert(report_pair).second) {
        pairs.push_back(std::move(report_pair));
        if (pairs.size() >= kMaxPathPairs) {
          return true;
        }
      }
    }
  }
  return false;
}

auto configuredPathPairs(const Net& net, const Config& config) -> std::vector<NodePair>
{
  std::set<NodePair> pairs;
  const auto pins = pinNames(net);
  const std::unordered_set<std::string> pin_set(pins.begin(), pins.end());

  auto add_from_to = [&](const std::string& from_pin, const std::string& to_pin) {
    if (pin_set.contains(from_pin) && pin_set.contains(to_pin)) {
      addPathPair(pairs, from_pin, to_pin);
    }
  };

  if (!config.from_pin.empty() && !config.to_pin.empty()) {
    add_from_to(config.from_pin, config.to_pin);
  }
  for (const auto& [from_pin, to_pin] : config.from_to_pins) {
    add_from_to(from_pin, to_pin);
  }

  std::vector<std::string> from_pins = config.from_pins;
  std::vector<std::string> to_pins = config.to_pins;
  if (!config.from_pin.empty()) {
    from_pins.push_back(config.from_pin);
  }
  if (!config.to_pin.empty()) {
    to_pins.push_back(config.to_pin);
  }

  if (!from_pins.empty() && !to_pins.empty()) {
    for (const auto& from_pin : from_pins) {
      for (const auto& to_pin : to_pins) {
        add_from_to(from_pin, to_pin);
      }
    }
  } else if (!from_pins.empty()) {
    for (const auto& from_pin : from_pins) {
      if (!pin_set.contains(from_pin)) {
        continue;
      }
      for (const auto& to_pin : pins) {
        addPathPair(pairs, from_pin, to_pin);
      }
    }
  } else if (!to_pins.empty()) {
    for (const auto& to_pin : to_pins) {
      if (!pin_set.contains(to_pin)) {
        continue;
      }
      for (const auto& from_pin : pins) {
        addPathPair(pairs, from_pin, to_pin);
      }
    }
  }

  return {pairs.begin(), pairs.end()};
}

auto defaultPathPairs(const Net& net) -> std::vector<NodePair>
{
  const auto pins = pinMap(net);
  const PinCounts counts = countPins(pins);

  std::set<NodePair> pair_set;
  std::vector<NodePair> pairs;
  if (appendSourceSinkPairs(net, pins, counts, pair_set, pairs)) {
    return pairs;
  }
  if (appendExternalInternalPairs(pins, pair_set, pairs)) {
    return pairs;
  }
  return pairs;
}

}  // namespace

PathPairGenerator::PathPairGenerator(const Config& config) : _config(config), _net_selector(config)
{
}

auto PathPairGenerator::generate(const Net& net) const -> std::vector<NodePair>
{
  if (_net_selector.hasPathFilter()) {
    return configuredPathPairs(net, _config);
  }
  return defaultPathPairs(net);
}

}  // namespace compare_spef
}  // namespace ircx
