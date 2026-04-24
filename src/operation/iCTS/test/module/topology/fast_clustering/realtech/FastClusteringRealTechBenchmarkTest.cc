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
 * @file FastClusteringRealTechBenchmarkTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech CTS clustering benchmark for fast and linear clustering.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "Log.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/linear_clustering/artifact/ClusterArtifactSupport.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/types/TestDataTypes.hh"
#include "common/visualization/TestVisualization.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Pin.hh"
#include "database/io/Wrapper.hh"
#include "database/spatial/Point.hh"
#include "dm_config.h"
#include "idm.h"
#include "module/topology/TopologyGen.hh"
#include "module/topology/clustering/Clustering.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/fast_clustering/FastClustering.hh"
#include "module/topology/linear_clustering/LinearClustering.hh"
#include "utils/logger/Schema.hh"

namespace icts_test::fast_clustering::realtech {
namespace {

using common::io::PrepareCleanOutputDir;
using common::io::ResolveOutputDir;
using common::io::SanitizeOutputName;
using common::io::WriteRawTextLog;
using common::logging::ScopedLogFile;

constexpr std::string_view kBenchmarkRoot = "/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008";
constexpr std::string_view kIcs55Workspace = "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev";
constexpr std::string_view kCtsConfigPath
    = "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json";
constexpr std::string_view kDefaultSdcPath = "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/default.sdc";
constexpr std::size_t kRequiredCaseCount = 20;
constexpr std::string_view kClusterSvgDirName = "cluster_svgs";

struct BenchmarkCase
{
  std::size_t index = 0;
  std::string case_name;
  std::string top_module;
  std::filesystem::path case_dir;
  std::filesystem::path def_path;
  std::filesystem::path verilog_path;
};

struct TechAssets
{
  std::filesystem::path pdk_root;
  std::filesystem::path cts_config_path;
  std::filesystem::path sdc_path;
  std::filesystem::path tech_lef_path;
  std::vector<std::filesystem::path> lef_paths;
  std::vector<std::filesystem::path> lib_paths;
};

struct LoadedCase
{
  bool ok = false;
  std::string error;
  int dbu_per_micron = 0;
  std::size_t inst_count = 0;
  std::size_t net_count = 0;
  std::size_t clock_count = 0;
  std::string clock_name;
  std::string clock_net_name;
  std::string clock_selection_reason;
  std::vector<icts::Pin*> loads;
  std::size_t load_count = 0;
  int span_diameter = 0;
};

struct ClockNetCandidate
{
  std::string net_name;
  std::string reason;
  std::size_t load_pin_count = 0;
  std::size_t inst_load_count = 0;
  std::size_t clock_pin_count = 0;
  bool idb_clock = false;
  bool name_clock_like = false;
  long long score = 0;
};

struct ResultMetrics
{
  std::string algorithm;
  double runtime_ms = 0.0;
  bool legal = false;
  std::size_t expected_load_count = 0;
  std::size_t load_count = 0;
  std::size_t missing_load_count = 0;
  std::size_t cluster_count = 0;
  std::size_t singleton_count = 0;
  std::size_t max_fanout = 0;
  int max_diameter = 0;
  std::size_t fanout_violations = 0;
  std::size_t diameter_violations = 0;
  std::size_t cap_violations = 0;
  std::size_t route_failures = 0;
  double total_score = 0.0;
  double total_wirelength = 0.0;
  double total_routing_cap_proxy = 0.0;
  double avg_routing_cap_proxy = 0.0;
  double routing_cap_proxy_variance = 0.0;
  double routing_cap_proxy_stddev = 0.0;
};

struct CaseResult
{
  BenchmarkCase benchmark_case;
  LoadedCase loaded;
  ResultMetrics linear;
  ResultMetrics fast;
  std::string cluster_svg;
};

struct ClusterRunResult
{
  ResultMetrics metrics;
  icts::ClusterResult result;
};

struct ClusterSvgArtifacts
{
  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::vector<std::size_t> cluster_sizes;
};

auto EndsWith(std::string_view text, std::string_view suffix) -> bool
{
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

auto TrimAscii(const std::string& text) -> std::string
{
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1U);
}

auto ToLowerAscii(std::string text) -> std::string
{
  std::ranges::transform(text, text.begin(), [](unsigned char ch) -> char { return static_cast<char>(std::tolower(ch)); });
  return text;
}

auto ContainsClockToken(std::string_view name) -> bool
{
  const auto lowered = ToLowerAscii(std::string(name));
  return lowered == "clk" || lowered == "clock" || lowered.find("clk") != std::string::npos || lowered.find("clock") != std::string::npos;
}

auto TryParseVerilogTopModule(const std::filesystem::path& verilog_path) -> std::string
{
  std::ifstream input_stream(verilog_path);
  if (!input_stream) {
    return {};
  }

  std::string line;
  while (std::getline(input_stream, line)) {
    auto trimmed = TrimAscii(line);
    if (!trimmed.starts_with("module ")) {
      continue;
    }
    std::istringstream line_stream(trimmed);
    std::string keyword;
    std::string module_name;
    line_stream >> keyword >> module_name;
    while (!module_name.empty() && (module_name.back() == '(' || module_name.back() == ';')) {
      module_name.pop_back();
    }
    return module_name;
  }
  return {};
}

auto StripDefSuffix(const std::string& filename) -> std::string
{
  if (EndsWith(filename, ".def.gz")) {
    return filename.substr(0U, filename.size() - std::string_view(".def.gz").size());
  }
  if (EndsWith(filename, ".def")) {
    return filename.substr(0U, filename.size() - std::string_view(".def").size());
  }
  return filename;
}

auto DiscoverBenchmarkCases() -> std::vector<BenchmarkCase>
{
  std::vector<BenchmarkCase> cases;
  const std::filesystem::path root(kBenchmarkRoot);
  std::error_code error_code;
  if (!std::filesystem::exists(root, error_code)) {
    return cases;
  }

  std::vector<std::filesystem::path> case_dirs;
  for (std::filesystem::directory_iterator it(root, error_code), end; !error_code && it != end; it.increment(error_code)) {
    if (it->is_directory(error_code)) {
      case_dirs.push_back(it->path());
    }
  }
  std::ranges::sort(case_dirs);

  for (const auto& case_dir : case_dirs) {
    const auto output_dir = case_dir / "place_dreamplace" / "output";
    if (!std::filesystem::is_directory(output_dir, error_code)) {
      continue;
    }

    std::vector<std::filesystem::path> def_paths;
    for (std::filesystem::directory_iterator it(output_dir, error_code), end; !error_code && it != end; it.increment(error_code)) {
      if (!it->is_regular_file(error_code)) {
        continue;
      }
      const auto filename = it->path().filename().string();
      if ((EndsWith(filename, "_place.def.gz") || EndsWith(filename, "_place.def"))) {
        def_paths.push_back(it->path());
      }
    }
    std::ranges::sort(def_paths);

    for (const auto& def_path : def_paths) {
      const auto def_base = StripDefSuffix(def_path.filename().string());
      const auto verilog_path = output_dir / (def_base + ".v");
      if (!std::filesystem::exists(verilog_path, error_code)) {
        continue;
      }
      const auto top_module = TryParseVerilogTopModule(verilog_path);
      if (top_module.empty()) {
        continue;
      }
      cases.push_back(BenchmarkCase{.index = cases.size() + 1U,
                                    .case_name = case_dir.filename().string(),
                                    .top_module = top_module,
                                    .case_dir = case_dir,
                                    .def_path = def_path,
                                    .verilog_path = verilog_path});
      break;
    }
    if (cases.size() == kRequiredCaseCount) {
      break;
    }
  }
  return cases;
}

auto ReadEnvPath(std::string_view key) -> std::filesystem::path
{
  const auto env_key = std::string(key);
  const char* value = std::getenv(env_key.c_str());
  return value == nullptr || *value == '\0' ? std::filesystem::path{} : std::filesystem::path(value);
}

auto FirstExistingPath(const std::vector<std::filesystem::path>& candidates) -> std::filesystem::path
{
  std::error_code error_code;
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate, error_code)) {
      return candidate;
    }
  }
  return {};
}

auto ResolveTechAssets() -> TechAssets
{
  TechAssets assets;
  assets.cts_config_path = std::filesystem::path(kCtsConfigPath);
  assets.sdc_path = std::filesystem::path(kDefaultSdcPath);
  assets.pdk_root = FirstExistingPath({
      ReadEnvPath("ICTS_REALTECH_PDK_DIR"),
      ReadEnvPath("PDK_DIR"),
      std::filesystem::path("/home/liweiguo/pdk/icsprout55-pdk"),
      std::filesystem::path("/home/liweiguo/pdk"),
  });

  assets.tech_lef_path = FirstExistingPath({
      assets.pdk_root / "prtech/techLEF/N551P6M_ieda.lef",
      assets.pdk_root / "prtech/techLEF/N551P6M_ecos.lef",
  });
  assets.lef_paths = {
      FirstExistingPath({
          assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR_ieda.lef",
          assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR_ecos.lef",
      }),
      FirstExistingPath({
          assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL_ieda.lef",
          assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL_ecos.lef",
      }),
  };
  assets.lib_paths = {
      assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib",
      assets.pdk_root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib",
  };
  return assets;
}

auto ValidateTechAssets(const TechAssets& assets, std::string& error) -> bool
{
  std::vector<std::string> missing;
  auto check_file = [&missing](std::string_view label, const std::filesystem::path& path) -> void {
    std::error_code error_code;
    if (path.empty() || !std::filesystem::exists(path, error_code)) {
      missing.push_back(std::string(label) + "=" + path.string());
    }
  };

  check_file("pdk_root", assets.pdk_root);
  check_file("cts_config", assets.cts_config_path);
  check_file("sdc", assets.sdc_path);
  check_file("tech_lef", assets.tech_lef_path);
  for (const auto& lef_path : assets.lef_paths) {
    check_file("lef", lef_path);
  }
  for (const auto& lib_path : assets.lib_paths) {
    check_file("lib", lib_path);
  }
  if (missing.empty()) {
    return true;
  }

  std::ostringstream stream;
  for (std::size_t index = 0; index < missing.size(); ++index) {
    if (index > 0U) {
      stream << "; ";
    }
    stream << missing.at(index);
  }
  error = stream.str();
  return false;
}

auto LoadTechnologyOnce(const TechAssets& assets, std::string& error) -> bool
{
  static bool loaded = false;
  if (loaded) {
    return true;
  }

  if (!dmInst->readLef(std::vector<std::string>{assets.tech_lef_path.string()}, true)) {
    error = "readLef(tech) failed";
    return false;
  }

  std::vector<std::string> lef_paths;
  lef_paths.reserve(assets.lef_paths.size());
  for (const auto& lef_path : assets.lef_paths) {
    lef_paths.push_back(lef_path.string());
  }
  if (!dmInst->readLef(lef_paths, false)) {
    error = "readLef(cells) failed";
    return false;
  }

  loaded = true;
  return true;
}

auto SetupDataManagerPaths(const TechAssets& assets, const BenchmarkCase& benchmark_case, const std::filesystem::path& output_dir) -> void
{
  auto& dm_config = dmInst->get_config();
  dm_config.set_tech_lef_path(assets.tech_lef_path.string());

  std::vector<std::string> lef_paths = {assets.tech_lef_path.string()};
  for (const auto& lef_path : assets.lef_paths) {
    lef_paths.push_back(lef_path.string());
  }
  dm_config.set_lef_paths(lef_paths);
  dm_config.set_def_path(benchmark_case.def_path.string());

  std::vector<std::string> lib_paths;
  lib_paths.reserve(assets.lib_paths.size());
  for (const auto& lib_path : assets.lib_paths) {
    lib_paths.push_back(lib_path.string());
  }
  dm_config.set_lib_paths(lib_paths);
  dm_config.set_sdc_path(assets.sdc_path.string());
  dm_config.set_output_path((output_dir / "dm_output" / SanitizeOutputName(benchmark_case.case_name)).string());
}

auto BuildClockNetCandidate(idb::IdbNet* idb_net) -> ClockNetCandidate
{
  ClockNetCandidate candidate;
  if (idb_net == nullptr) {
    return candidate;
  }

  candidate.net_name = idb_net->get_net_name();
  candidate.idb_clock = idb_net->is_clock();
  candidate.name_clock_like = ContainsClockToken(candidate.net_name);
  const auto idb_load_pins = idb_net->get_load_pins();
  candidate.load_pin_count = idb_load_pins.size();
  for (auto* idb_pin : idb_load_pins) {
    if (idb_pin == nullptr || idb_pin->get_instance() == nullptr) {
      continue;
    }
    ++candidate.inst_load_count;
    if (idb_pin->is_flip_flop_clk()) {
      ++candidate.clock_pin_count;
    }
  }

  candidate.score = static_cast<long long>(candidate.inst_load_count);
  if (candidate.name_clock_like) {
    candidate.score += 10'000'000'000LL;
  }
  if (candidate.clock_pin_count > 0U) {
    candidate.score += 100'000'000'000LL + static_cast<long long>(candidate.clock_pin_count) * 1'000'000LL;
  }
  if (candidate.idb_clock) {
    candidate.score += 1'000'000'000'000LL;
  }

  std::ostringstream reason_stream;
  reason_stream << "idb_clock=" << candidate.idb_clock << ",name_clock_like=" << candidate.name_clock_like
                << ",clock_pin_count=" << candidate.clock_pin_count << ",inst_load_count=" << candidate.inst_load_count;
  candidate.reason = reason_stream.str();
  return candidate;
}

auto SelectClockNetCandidate(idb::IdbDesign* idb_design) -> std::optional<ClockNetCandidate>
{
  if (idb_design == nullptr || idb_design->get_net_list() == nullptr) {
    return std::nullopt;
  }

  std::optional<ClockNetCandidate> best_candidate = std::nullopt;
  for (auto* idb_net : idb_design->get_net_list()->get_net_list()) {
    if (idb_net == nullptr || idb_net->is_pdn()) {
      continue;
    }
    auto candidate = BuildClockNetCandidate(idb_net);
    if (candidate.inst_load_count <= 1U) {
      continue;
    }
    if (!best_candidate.has_value() || candidate.score > best_candidate->score
        || (candidate.score == best_candidate->score && candidate.net_name < best_candidate->net_name)) {
      best_candidate = std::move(candidate);
    }
  }
  return best_candidate;
}

auto CalcClusterDiameter(const std::vector<icts::Pin*>& loads) -> int
{
  if (loads.empty()) {
    return 0;
  }

  int min_x = loads.front()->get_location().get_x();
  int min_y = loads.front()->get_location().get_y();
  int max_x = min_x;
  int max_y = min_y;
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_x = std::max(max_x, location.get_x());
    max_y = std::max(max_y, location.get_y());
  }
  return (max_x - min_x) + (max_y - min_y);
}

auto CalcManhattanDistance(const icts::Point<int>& lhs, const icts::Point<int>& rhs) -> double
{
  const auto dx = std::abs(static_cast<long long>(lhs.get_x()) - static_cast<long long>(rhs.get_x()));
  const auto dy = std::abs(static_cast<long long>(lhs.get_y()) - static_cast<long long>(rhs.get_y()));
  return static_cast<double>(dx + dy);
}

auto CalcClusterCenter(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>
{
  long long x_sum = 0;
  long long y_sum = 0;
  std::size_t pin_count = 0;
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    x_sum += location.get_x();
    y_sum += location.get_y();
    ++pin_count;
  }
  if (pin_count == 0U) {
    return {0, 0};
  }
  return {static_cast<int>(std::lround(static_cast<double>(x_sum) / static_cast<double>(pin_count))),
          static_cast<int>(std::lround(static_cast<double>(y_sum) / static_cast<double>(pin_count)))};
}

auto CalcClusterMedian(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>
{
  std::vector<int> x_coords;
  std::vector<int> y_coords;
  x_coords.reserve(cluster.size());
  y_coords.reserve(cluster.size());
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    x_coords.push_back(location.get_x());
    y_coords.push_back(location.get_y());
  }
  if (x_coords.empty()) {
    return {0, 0};
  }

  const auto middle = static_cast<std::ptrdiff_t>(x_coords.size() / 2U);
  auto x_middle = x_coords.begin() + middle;
  auto y_middle = y_coords.begin() + middle;
  std::nth_element(x_coords.begin(), x_middle, x_coords.end());
  std::nth_element(y_coords.begin(), y_middle, y_coords.end());
  return {*x_middle, *y_middle};
}

auto ResolveClusterRoot(const std::vector<icts::Pin*>& cluster, const icts::LinearClusteringConfig& config) -> icts::Point<int>
{
  return config.root_policy == icts::LinearRootPolicy::kCenter ? CalcClusterCenter(cluster) : CalcClusterMedian(cluster);
}

auto CalcRoutingCapProxy(const std::vector<icts::Pin*>& cluster, const icts::LinearClusteringConfig& config) -> double
{
  if (cluster.size() <= 1U) {
    return 0.0;
  }

  const auto root = ResolveClusterRoot(cluster, config);
  double proxy = 0.0;
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    proxy += CalcManhattanDistance(root, pin->get_location());
  }
  return proxy;
}

auto CalcPopulationVariance(const std::vector<double>& values, double mean) -> double
{
  if (values.empty()) {
    return 0.0;
  }

  double variance = 0.0;
  for (const auto value : values) {
    const auto delta = value - mean;
    variance += delta * delta;
  }
  return variance / static_cast<double>(values.size());
}

auto LoadBenchmarkCase(const BenchmarkCase& benchmark_case, const TechAssets& assets, const std::filesystem::path& output_dir) -> LoadedCase
{
  LoadedCase loaded;
  CONFIG_INST.reset();
  CONFIG_INST.init(assets.cts_config_path.string());
  CONFIG_INST.set_work_dir((output_dir / "cts_workspace" / SanitizeOutputName(benchmark_case.case_name)).string());

  std::string tech_error;
  if (!LoadTechnologyOnce(assets, tech_error)) {
    loaded.error = tech_error;
    return loaded;
  }

  if (!dmInst->readVerilog(benchmark_case.verilog_path.string(), benchmark_case.top_module)) {
    loaded.error = "readVerilog failed";
    return loaded;
  }
  if (!dmInst->readDef(benchmark_case.def_path.string())) {
    loaded.error = "readDef failed";
    return loaded;
  }

  SetupDataManagerPaths(assets, benchmark_case, output_dir);
  auto* idb_builder = dmInst->get_idb_builder();
  if (idb_builder == nullptr) {
    loaded.error = "idb builder is null after placement load";
    return loaded;
  }

  STA_ADAPTER_INST.init();

  DESIGN_INST.reset();
  auto* idb_design = dmInst->get_idb_design();
  if (idb_design == nullptr) {
    loaded.error = "iDB design is null after load";
    return loaded;
  }
  if (idb_design->get_design_name() != benchmark_case.top_module) {
    loaded.error = "DEF/Verilog design mismatch: def=" + idb_design->get_design_name() + ", verilog=" + benchmark_case.top_module;
    return loaded;
  }

  const auto clock_candidate = SelectClockNetCandidate(idb_design);
  if (!clock_candidate.has_value()) {
    loaded.error = "no CTS load net candidate found";
    return loaded;
  }
  DESIGN_INST.add_clock(std::make_unique<icts::Clock>("benchmark_" + clock_candidate->net_name, clock_candidate->net_name));
  WRAPPER_INST.reset();
  WRAPPER_INST.init(idb_builder);
  WRAPPER_INST.read();

  loaded.dbu_per_micron = std::max(1, WRAPPER_INST.queryDbUnit());
  loaded.inst_count = idb_design->get_instance_list() == nullptr ? 0U : idb_design->get_instance_list()->get_instance_list().size();
  loaded.net_count = idb_design->get_net_list() == nullptr ? 0U : idb_design->get_net_list()->get_net_list().size();
  const auto clocks = DESIGN_INST.get_clocks();
  loaded.clock_count = clocks.size();
  for (auto* clock : clocks) {
    if (clock == nullptr || clock->get_loads().size() <= loaded.loads.size()) {
      continue;
    }
    loaded.clock_name = clock->get_clock_name();
    loaded.clock_net_name = clock->get_clock_net_name();
    loaded.loads = clock->get_loads();
    loaded.clock_selection_reason = clock_candidate->reason;
  }
  if (loaded.loads.empty()) {
    loaded.error = "no clock loads extracted";
    return loaded;
  }

  loaded.load_count = loaded.loads.size();
  loaded.span_diameter = CalcClusterDiameter(loaded.loads);
  loaded.ok = true;
  return loaded;
}

auto BuildBenchmarkConfig() -> icts::LinearClusteringConfig
{
  auto config = icts::TopologyGen::buildLinearClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), CONFIG_INST.get_max_cap());
  config.routing_layer = CONFIG_INST.get_routing_layers().empty() ? 0 : static_cast<int>(CONFIG_INST.get_routing_layers().back());
  config.wire_width = CONFIG_INST.get_wire_width();
  config.enable_exact_cap = false;
  config.always_build_exact_cap = false;
  return config;
}

auto ScoreCluster(const std::vector<icts::Pin*>& cluster, const icts::ClusterElectricalSummary* summary,
                  const icts::LinearClusteringConfig& config) -> double
{
  const auto diameter = CalcClusterDiameter(cluster);
  if (config.scoring_strategy == icts::LinearScoringStrategy::kTotalWirelength) {
    if (summary != nullptr && summary->wirelength_dbu > 0.0) {
      return config.wirelength_weight * summary->wirelength_dbu;
    }
    return config.wirelength_weight * static_cast<double>(diameter);
  }
  if (diameter > 0) {
    return static_cast<double>(diameter);
  }
  return config.max_diameter > 0 ? static_cast<double>(config.max_diameter) : 0.0;
}

auto EvaluateResult(const std::string& algorithm, const icts::ClusterResult& result, const icts::LinearClusteringConfig& config,
                    std::size_t expected_load_count, double runtime_ms) -> ResultMetrics
{
  ResultMetrics metrics;
  metrics.algorithm = algorithm;
  metrics.runtime_ms = runtime_ms;
  metrics.expected_load_count = expected_load_count;
  metrics.cluster_count = result.clusters.size();
  metrics.legal = !result.clusters.empty();

  std::vector<double> routing_cap_proxies;
  routing_cap_proxies.reserve(result.clusters.size());
  for (std::size_t cluster_id = 0; cluster_id < result.clusters.size(); ++cluster_id) {
    const auto& cluster = result.clusters.at(cluster_id);
    metrics.load_count += cluster.size();
    metrics.max_fanout = std::max(metrics.max_fanout, cluster.size());
    if (cluster.size() == 1U) {
      ++metrics.singleton_count;
    }
    const auto diameter = CalcClusterDiameter(cluster);
    metrics.max_diameter = std::max(metrics.max_diameter, diameter);
    if (config.max_fanout > 0U && cluster.size() > config.max_fanout) {
      ++metrics.fanout_violations;
    }
    if (config.max_diameter > 0 && diameter > config.max_diameter) {
      ++metrics.diameter_violations;
    }

    const auto* summary = cluster_id < result.electrical_summaries.size() ? &result.electrical_summaries.at(cluster_id) : nullptr;
    if (summary != nullptr) {
      metrics.total_wirelength += summary->wirelength_dbu;
      if (config.max_cap > 0.0 && std::isfinite(config.max_cap) && summary->total_cap_pf > config.max_cap) {
        ++metrics.cap_violations;
      }
      if (summary->exact && !summary->route_success) {
        ++metrics.route_failures;
      }
    }
    metrics.total_score += ScoreCluster(cluster, summary, config);
    const auto routing_cap_proxy = CalcRoutingCapProxy(cluster, config);
    metrics.total_routing_cap_proxy += routing_cap_proxy;
    routing_cap_proxies.push_back(routing_cap_proxy);
  }

  if (!routing_cap_proxies.empty()) {
    metrics.avg_routing_cap_proxy = metrics.total_routing_cap_proxy / static_cast<double>(routing_cap_proxies.size());
    metrics.routing_cap_proxy_variance = CalcPopulationVariance(routing_cap_proxies, metrics.avg_routing_cap_proxy);
    metrics.routing_cap_proxy_stddev = std::sqrt(metrics.routing_cap_proxy_variance);
  }

  if (metrics.load_count < metrics.expected_load_count) {
    metrics.missing_load_count = metrics.expected_load_count - metrics.load_count;
  }
  metrics.legal = metrics.legal && metrics.fanout_violations == 0U && metrics.diameter_violations == 0U && metrics.cap_violations == 0U
                  && metrics.route_failures == 0U && metrics.load_count == metrics.expected_load_count;
  return metrics;
}

template <typename Runner>
auto RunAndMeasure(const std::string& algorithm, const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config,
                   Runner runner) -> ClusterRunResult
{
  const auto start = std::chrono::steady_clock::now();
  auto result = runner(loads, config);
  const auto finish = std::chrono::steady_clock::now();
  const auto runtime_ms = std::chrono::duration<double, std::milli>(finish - start).count();
  return ClusterRunResult{.metrics = EvaluateResult(algorithm, result, config, loads.size(), runtime_ms), .result = std::move(result)};
}

auto CsvEscape(const std::string& text) -> std::string
{
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }

  std::string escaped = "\"";
  for (const auto ch : text) {
    if (ch == '"') {
      escaped += "\"\"";
    } else {
      escaped += ch;
    }
  }
  escaped += '"';
  return escaped;
}

auto FormatCaseIndex(std::size_t index) -> std::string
{
  std::ostringstream stream;
  stream << std::setw(2) << std::setfill('0') << index;
  return stream.str();
}

auto BuildClusterSvgName(const BenchmarkCase& benchmark_case) -> std::string
{
  return "case_" + FormatCaseIndex(benchmark_case.index) + "_" + SanitizeOutputName(benchmark_case.case_name) + "_clusters.svg";
}

auto BuildClusterSvgArtifacts(const icts::ClusterResult& result, const std::vector<icts::Pin*>& loads, ClusterSvgArtifacts& artifacts,
                              std::string& error) -> bool
{
  return common::linear_clustering::BuildClusterArtifacts(result, loads, artifacts.cluster_map, artifacts.centers, artifacts.cluster_sizes,
                                                          error);
}

auto WriteCaseClusterSvg(const std::filesystem::path& svg_dir, const BenchmarkCase& benchmark_case, const std::vector<icts::Pin*>& loads,
                         const icts::ClusterResult& linear_result, const icts::ClusterResult& fast_result, std::string& error)
    -> std::filesystem::path
{
  std::error_code error_code;
  std::filesystem::create_directories(svg_dir, error_code);
  if (error_code) {
    error = "failed to create cluster svg directory: " + error_code.message();
    return {};
  }

  ClusterSvgArtifacts linear_artifacts;
  if (!BuildClusterSvgArtifacts(linear_result, loads, linear_artifacts, error)) {
    error = "linear artifacts: " + error;
    return {};
  }
  ClusterSvgArtifacts fast_artifacts;
  if (!BuildClusterSvgArtifacts(fast_result, loads, fast_artifacts, error)) {
    error = "fast artifacts: " + error;
    return {};
  }

  const auto svg_path = svg_dir / BuildClusterSvgName(benchmark_case);
  const bool wrote_svg = common::visualization::WriteClusterComparisonSvg(
      svg_path.string(), loads, "linear clustering", linear_artifacts.cluster_map, linear_artifacts.centers, "fast clustering",
      fast_artifacts.cluster_map, fast_artifacts.centers);
  if (!wrote_svg) {
    error = "failed to write cluster comparison svg: " + svg_path.string();
    return {};
  }
  return svg_path;
}

auto BuildCasesCsv(const std::vector<CaseResult>& results) -> std::string
{
  std::ostringstream stream;
  stream << "index,case,top,def,verilog,dbu_per_micron,inst_count,net_count,clock_count,clock_name,clock_net,"
            "clock_selection_reason,load_count,span_diameter\n";
  for (const auto& result : results) {
    stream << result.benchmark_case.index << "," << CsvEscape(result.benchmark_case.case_name) << ","
           << CsvEscape(result.benchmark_case.top_module) << "," << CsvEscape(result.benchmark_case.def_path.string()) << ","
           << CsvEscape(result.benchmark_case.verilog_path.string()) << "," << result.loaded.dbu_per_micron << ","
           << result.loaded.inst_count << "," << result.loaded.net_count << "," << result.loaded.clock_count << ","
           << CsvEscape(result.loaded.clock_name) << "," << CsvEscape(result.loaded.clock_net_name) << ","
           << CsvEscape(result.loaded.clock_selection_reason) << "," << result.loaded.load_count << "," << result.loaded.span_diameter
           << "\n";
  }
  return stream.str();
}

auto AppendMetricsCsvRow(std::ostringstream& stream, const BenchmarkCase& benchmark_case, const ResultMetrics& metrics) -> void
{
  stream << benchmark_case.index << "," << CsvEscape(benchmark_case.case_name) << "," << metrics.algorithm << "," << std::fixed
         << std::setprecision(3) << metrics.runtime_ms << "," << std::setprecision(6) << metrics.total_score << "," << metrics.legal << ","
         << metrics.expected_load_count << "," << metrics.load_count << "," << metrics.missing_load_count << "," << metrics.cluster_count
         << "," << metrics.singleton_count << "," << metrics.max_fanout << "," << metrics.max_diameter << "," << metrics.fanout_violations
         << "," << metrics.diameter_violations << "," << metrics.cap_violations << "," << metrics.route_failures << ","
         << metrics.total_wirelength << "," << metrics.total_routing_cap_proxy << "," << metrics.avg_routing_cap_proxy << ","
         << metrics.routing_cap_proxy_variance << "," << metrics.routing_cap_proxy_stddev << "\n";
}

auto BuildComparisonCsv(const std::vector<CaseResult>& results) -> std::string
{
  std::ostringstream stream;
  stream << "index,case,algorithm,runtime_ms,total_score,legal,expected_load_count,load_count,missing_load_count,"
            "cluster_count,singleton_count,max_fanout,max_diameter,"
            "fanout_violations,diameter_violations,cap_violations,route_failures,total_wirelength,"
            "total_routing_cap_proxy,avg_routing_cap_proxy,routing_cap_proxy_variance,routing_cap_proxy_stddev\n";
  for (const auto& result : results) {
    AppendMetricsCsvRow(stream, result.benchmark_case, result.linear);
    AppendMetricsCsvRow(stream, result.benchmark_case, result.fast);
  }
  return stream.str();
}

auto BuildVisualizationCsv(const std::vector<CaseResult>& results) -> std::string
{
  std::ostringstream stream;
  stream << "index,case,cluster_svg\n";
  for (const auto& result : results) {
    stream << result.benchmark_case.index << "," << CsvEscape(result.benchmark_case.case_name) << "," << CsvEscape(result.cluster_svg)
           << "\n";
  }
  return stream.str();
}

auto BuildRankingCsv(double linear_runtime_ms, double fast_runtime_ms, double linear_score, double fast_score,
                     double linear_routing_cap_proxy_variance, double fast_routing_cap_proxy_variance) -> std::string
{
  std::ostringstream stream;
  stream << "rank,algorithm,total_runtime_ms,total_score,routing_cap_proxy_variance_sum\n";
  if (fast_runtime_ms < linear_runtime_ms) {
    stream << "1,fast," << fast_runtime_ms << "," << fast_score << "," << fast_routing_cap_proxy_variance << "\n";
    stream << "2,linear," << linear_runtime_ms << "," << linear_score << "," << linear_routing_cap_proxy_variance << "\n";
  } else {
    stream << "1,linear," << linear_runtime_ms << "," << linear_score << "," << linear_routing_cap_proxy_variance << "\n";
    stream << "2,fast," << fast_runtime_ms << "," << fast_score << "," << fast_routing_cap_proxy_variance << "\n";
  }
  return stream.str();
}

auto BuildInventoryReport(const std::vector<BenchmarkCase>& cases, const TechAssets& assets) -> std::string
{
  std::ostringstream stream;
  stream << "benchmark_root=" << kBenchmarkRoot << "\n";
  stream << "case_count=" << cases.size() << "\n";
  stream << "pdk_root=" << assets.pdk_root.string() << "\n";
  stream << "cts_config=" << assets.cts_config_path.string() << "\n";
  stream << "sdc=" << assets.sdc_path.string() << "\n";
  stream << "tech_lef=" << assets.tech_lef_path.string() << "\n";
  for (std::size_t index = 0; index < cases.size(); ++index) {
    const auto& benchmark_case = cases.at(index);
    stream << "case_" << (index + 1U) << "=" << benchmark_case.case_name << ",top=" << benchmark_case.top_module
           << ",def=" << benchmark_case.def_path.string() << ",verilog=" << benchmark_case.verilog_path.string() << "\n";
  }
  return stream.str();
}

auto BuildLoadedCaseReport(const BenchmarkCase& benchmark_case, const LoadedCase& loaded) -> std::string
{
  std::ostringstream stream;
  stream << "index=" << benchmark_case.index << "\n";
  stream << "case=" << benchmark_case.case_name << "\n";
  stream << "top=" << benchmark_case.top_module << "\n";
  stream << "dbu_per_micron=" << loaded.dbu_per_micron << "\n";
  stream << "inst_count=" << loaded.inst_count << "\n";
  stream << "net_count=" << loaded.net_count << "\n";
  stream << "clock_count=" << loaded.clock_count << "\n";
  stream << "clock_name=" << loaded.clock_name << "\n";
  stream << "clock_net=" << loaded.clock_net_name << "\n";
  stream << "clock_selection_reason=" << loaded.clock_selection_reason << "\n";
  stream << "load_count=" << loaded.load_count << "\n";
  stream << "span_diameter=" << loaded.span_diameter << "\n";
  return stream.str();
}

auto SumLinearRoutingCapProxyVariance(const std::vector<CaseResult>& results) -> double
{
  double total = 0.0;
  for (const auto& result : results) {
    total += result.linear.routing_cap_proxy_variance;
  }
  return total;
}

auto SumFastRoutingCapProxyVariance(const std::vector<CaseResult>& results) -> double
{
  double total = 0.0;
  for (const auto& result : results) {
    total += result.fast.routing_cap_proxy_variance;
  }
  return total;
}

auto BuildSummaryReport(const std::vector<CaseResult>& results, double linear_runtime_ms, double fast_runtime_ms, double linear_score,
                        double fast_score) -> std::string
{
  std::size_t fast_runtime_wins = 0;
  std::size_t fast_score_wins = 0;
  std::size_t fast_routing_cap_variance_wins = 0;
  std::size_t legal_cases = 0;
  const auto linear_routing_cap_variance = SumLinearRoutingCapProxyVariance(results);
  const auto fast_routing_cap_variance = SumFastRoutingCapProxyVariance(results);
  for (const auto& result : results) {
    if (result.linear.legal && result.fast.legal) {
      ++legal_cases;
    }
    if (result.fast.runtime_ms < result.linear.runtime_ms) {
      ++fast_runtime_wins;
    }
    if (result.fast.total_score < result.linear.total_score) {
      ++fast_score_wins;
    }
    if (result.fast.routing_cap_proxy_variance < result.linear.routing_cap_proxy_variance) {
      ++fast_routing_cap_variance_wins;
    }
  }

  std::ostringstream stream;
  stream << "case_count=" << results.size() << "\n";
  stream << "legal_case_count=" << legal_cases << "\n";
  stream << "linear_total_runtime_ms=" << linear_runtime_ms << "\n";
  stream << "fast_total_runtime_ms=" << fast_runtime_ms << "\n";
  stream << "runtime_speedup=" << (fast_runtime_ms > 0.0 ? linear_runtime_ms / fast_runtime_ms : 0.0) << "\n";
  stream << "linear_total_score=" << linear_score << "\n";
  stream << "fast_total_score=" << fast_score << "\n";
  stream << "score_improvement_ratio=" << (linear_score > 0.0 ? (linear_score - fast_score) / linear_score : 0.0) << "\n";
  stream << "linear_routing_cap_proxy_variance_sum=" << linear_routing_cap_variance << "\n";
  stream << "fast_routing_cap_proxy_variance_sum=" << fast_routing_cap_variance << "\n";
  stream << "routing_cap_proxy_variance_improvement_ratio="
         << (linear_routing_cap_variance > 0.0 ? (linear_routing_cap_variance - fast_routing_cap_variance) / linear_routing_cap_variance
                                               : 0.0)
         << "\n";
  stream << "fast_runtime_case_wins=" << fast_runtime_wins << "\n";
  stream << "fast_score_case_wins=" << fast_score_wins << "\n";
  stream << "fast_routing_cap_variance_case_wins=" << fast_routing_cap_variance_wins << "\n";
  stream << "acceptance_runtime=" << (fast_runtime_ms < linear_runtime_ms ? "pass" : "fail") << "\n";
  stream << "acceptance_score=" << (fast_score < linear_score ? "pass" : "fail") << "\n";
  stream << "acceptance_routing_cap_variance=" << (fast_routing_cap_variance < linear_routing_cap_variance ? "pass" : "fail") << "\n";
  return stream.str();
}

TEST(FastClusteringRealTechBenchmarkTest, CompareTwentyPlacementCases)
{
  const auto output_dir = PrepareCleanOutputDir(ResolveOutputDir() / "fast_clustering" / "realtech_benchmark" / "current_run");
  const auto cts_log_path = output_dir / "cts.log";
  ScopedLogFile scoped_log(cts_log_path, "Fast Clustering RealTech Benchmark");

  const auto cases = DiscoverBenchmarkCases();
  const auto assets = ResolveTechAssets();
  common::io::EmitInfoReport(InfoReport{.title = "CTS Clustering Benchmark Inventory", .content = BuildInventoryReport(cases, assets)});

  if (cases.empty()) {
    GTEST_SKIP() << "benchmark root unavailable: " << kBenchmarkRoot;
  }
  ASSERT_EQ(cases.size(), kRequiredCaseCount);

  std::string tech_error;
  ASSERT_TRUE(ValidateTechAssets(assets, tech_error)) << tech_error;

  std::vector<CaseResult> results;
  results.reserve(cases.size());
  double linear_runtime_ms = 0.0;
  double fast_runtime_ms = 0.0;
  double linear_score = 0.0;
  double fast_score = 0.0;
  const auto svg_dir = output_dir / std::string(kClusterSvgDirName);

  for (const auto& benchmark_case : cases) {
    auto loaded = LoadBenchmarkCase(benchmark_case, assets, output_dir);
    ASSERT_TRUE(loaded.ok) << benchmark_case.case_name << ": " << loaded.error;
    common::io::EmitInfoReport(
        InfoReport{.title = "CTS Clustering Case Statistics", .content = BuildLoadedCaseReport(benchmark_case, loaded)});

    auto config = BuildBenchmarkConfig();
    auto linear_run = RunAndMeasure("linear", loaded.loads, config,
                                    [](const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& run_config) {
                                      return icts::LinearClustering::runDefault(loads, run_config);
                                    });
    auto fast_run = RunAndMeasure("fast", loaded.loads, config,
                                  [](const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& run_config) {
                                    return icts::FastClustering::runDefault(loads, run_config);
                                  });

    linear_runtime_ms += linear_run.metrics.runtime_ms;
    fast_runtime_ms += fast_run.metrics.runtime_ms;
    linear_score += linear_run.metrics.total_score;
    fast_score += fast_run.metrics.total_score;

    std::string svg_error;
    const auto svg_path = WriteCaseClusterSvg(svg_dir, benchmark_case, loaded.loads, linear_run.result, fast_run.result, svg_error);
    EXPECT_FALSE(svg_path.empty()) << benchmark_case.case_name << ": " << svg_error;
    std::string cluster_svg;
    if (!svg_path.empty()) {
      cluster_svg = (std::filesystem::path(std::string(kClusterSvgDirName)) / svg_path.filename()).string();
      icts::schema::EmitArtifact("CTS clustering structure svg", svg_path);
    }

    loaded.loads.clear();
    results.push_back(CaseResult{.benchmark_case = benchmark_case,
                                 .loaded = std::move(loaded),
                                 .linear = std::move(linear_run.metrics),
                                 .fast = std::move(fast_run.metrics),
                                 .cluster_svg = std::move(cluster_svg)});
  }

  const auto linear_routing_cap_variance = SumLinearRoutingCapProxyVariance(results);
  const auto fast_routing_cap_variance = SumFastRoutingCapProxyVariance(results);
  auto summary = BuildSummaryReport(results, linear_runtime_ms, fast_runtime_ms, linear_score, fast_score);
  summary += "visualization_svg_dir=" + std::string(kClusterSvgDirName) + "\n";
  summary += "visualization_svg_count=" + std::to_string(results.size()) + "\n";
  WriteRawTextLog(output_dir / "cts_clustering_cases.csv", BuildCasesCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_comparison.csv", BuildComparisonCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_visualizations.csv", BuildVisualizationCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_ranking.csv", BuildRankingCsv(linear_runtime_ms, fast_runtime_ms, linear_score, fast_score,
                                                                             linear_routing_cap_variance, fast_routing_cap_variance));
  WriteRawTextLog(output_dir / "report.log", summary);
  common::io::EmitInfoReport(InfoReport{.title = "CTS Clustering Benchmark Summary", .content = summary});

  ASSERT_LT(fast_runtime_ms, linear_runtime_ms);
  ASSERT_LT(fast_score, linear_score);
  ASSERT_LT(fast_routing_cap_variance, linear_routing_cap_variance);
  for (const auto& result : results) {
    EXPECT_TRUE(result.linear.legal) << result.benchmark_case.case_name << " linear illegal";
    EXPECT_TRUE(result.fast.legal) << result.benchmark_case.case_name << " fast illegal";
  }
}

}  // namespace
}  // namespace icts_test::fast_clustering::realtech
