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
 * @file RealTechAssetLoader.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Asset probing and environment bootstrap support for real-tech tests.
 */

#include "common/realtech/asset/RealTechAssetLoader.hh"

#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/io/Wrapper.hh"
#include "dm_config.h"
#include "idm.h"
#include "utils/logger/Logger.hh"

namespace icts_test::common::realtech::asset {
namespace {

struct RealTechAssets
{
  std::filesystem::path config_path;
  std::filesystem::path tech_lef_path;
  std::vector<std::filesystem::path> lef_paths;
  std::filesystem::path def_path;
  std::vector<std::filesystem::path> lib_paths;
  std::filesystem::path sdc_path;
  std::filesystem::path output_dir;
};

constexpr std::string_view kIcs55WorkspacePath = "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev";
constexpr std::string_view kIcs55PdkPath = "/home/liweiguo/PDK/icsprout55-pdk";
constexpr std::string_view kIcs55FlowScriptPath
    = "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl";

auto CollectCandidateAssets() -> std::vector<RealTechAssets>
{
  const std::filesystem::path workspace(kIcs55WorkspacePath);
  const std::filesystem::path pdk_dir(kIcs55PdkPath);

  RealTechAssets assets;
  assets.config_path = std::filesystem::path(kIcs55FlowScriptPath);
  assets.tech_lef_path = pdk_dir / "prtech/techLEF/N551P6M_ecos.lef";
  assets.lef_paths = {
      pdk_dir / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR_ecos.lef",
      pdk_dir / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL_ecos.lef",
  };
  assets.def_path = workspace / "result/iPL_result.def";
  assets.lib_paths = {
      pdk_dir / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib",
      pdk_dir / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib",
  };
  assets.sdc_path = workspace / "default.sdc";
  assets.output_dir = workspace / "result";

  return {assets};
}

auto ValidateAssets(const RealTechAssets& assets, std::string& error) -> bool
{
  std::vector<std::string> missing_entries;
  auto check_file = [&missing_entries](const std::string& label, const std::filesystem::path& path) -> void {
    if (path.empty() || !std::filesystem::exists(path)) {
      missing_entries.emplace_back(label + "=" + path.string());
    }
  };

  check_file("flow_script", assets.config_path);
  check_file("tech_lef", assets.tech_lef_path);
  check_file("def", assets.def_path);
  check_file("sdc", assets.sdc_path);
  if (assets.lef_paths.empty()) {
    missing_entries.emplace_back("lef_paths=<empty>");
  } else {
    for (const auto& lef_path : assets.lef_paths) {
      check_file("lef", lef_path);
    }
  }
  if (assets.lib_paths.empty()) {
    missing_entries.emplace_back("lib_path=<empty>");
  } else {
    for (const auto& lib_path : assets.lib_paths) {
      check_file("lib", lib_path);
    }
  }

  if (missing_entries.empty()) {
    return true;
  }

  std::ostringstream stream;
  stream << "missing assets: ";
  for (std::size_t index = 0; index < missing_entries.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << missing_entries.at(index);
  }
  error = stream.str();
  return false;
}

auto LoadRealTechAssets(const RealTechAssets& assets, std::string& error) -> bool
{
  const std::vector<std::string> tech_lef_paths = {assets.tech_lef_path.string()};
  if (!dmInst->readLef(tech_lef_paths, true)) {
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

  if (!dmInst->readDef(assets.def_path.string())) {
    error = "readDef failed";
    return false;
  }

  auto& dm_config = dmInst->get_config();
  dm_config.set_tech_lef_path(assets.tech_lef_path.string());

  std::vector<std::string> all_lef_paths;
  all_lef_paths.reserve(assets.lef_paths.size() + 1);
  all_lef_paths.push_back(assets.tech_lef_path.string());
  for (const auto& lef_path : assets.lef_paths) {
    if (lef_path != assets.tech_lef_path) {
      all_lef_paths.push_back(lef_path.string());
    }
  }
  dm_config.set_lef_paths(all_lef_paths);
  dm_config.set_def_path(assets.def_path.string());

  std::vector<std::string> lib_paths;
  lib_paths.reserve(assets.lib_paths.size());
  for (const auto& lib_path : assets.lib_paths) {
    lib_paths.push_back(lib_path.string());
  }
  dm_config.set_lib_paths(lib_paths);
  dm_config.set_sdc_path(assets.sdc_path.string());

  const auto work_dir = io::ResolveLinearClusteringOutputDir() / "real_tech_workspace";
  std::error_code error_code;
  std::filesystem::create_directories(work_dir, error_code);
  if (error_code) {
    error = "cannot create work dir: " + work_dir.string();
    return false;
  }
  CONFIG_INST.set_work_dir(work_dir.string());
  dm_config.set_output_path((io::ResolveLinearClusteringOutputDir() / "real_tech_output").string());

  auto* idb_builder = dmInst->get_idb_builder();
  if (idb_builder == nullptr) {
    error = "idb builder is null after LEF/DEF load";
    return false;
  }

  WRAPPER_INST.reset();
  WRAPPER_INST.init(idb_builder);
  WRAPPER_INST.read();
  STA_ADAPTER_INST.init();
  return true;
}

}  // namespace

auto BuildRealTechSetupState() -> RealTechSetupState
{
  RealTechSetupState state;
  state.output_dir = io::ResolveLinearClusteringOutputDir();
  std::error_code error_code;
  std::filesystem::create_directories(state.output_dir, error_code);

  std::vector<std::string> probe_errors;
  const auto candidates = CollectCandidateAssets();
  for (const auto& assets : candidates) {
    const auto& flow_script_path = assets.config_path;

    std::string validation_error;
    if (!ValidateAssets(assets, validation_error)) {
      probe_errors.emplace_back(flow_script_path.string() + " invalid: " + validation_error);
      continue;
    }

    state.assets_available = true;
    state.config_path = flow_script_path;

    std::string load_error;
    if (LoadRealTechAssets(assets, load_error)) {
      state.mode = RealTechMode::kRealTech;
      state.setup_succeeded = true;
      state.source_label = "real_tech:" + flow_script_path.string();
      state.summary = "loaded real tech/design from ICS55 flow paths rooted at " + flow_script_path.string();
      CTS_LOG_INFO << "RealTechSetup: " << state.summary;
      return state;
    }

    probe_errors.emplace_back(flow_script_path.string() + " load failed: " + load_error);
  }

  std::ostringstream summary;
  summary << "fallback to synthetic sweeps";
  if (!probe_errors.empty()) {
    summary << " (";
    for (std::size_t index = 0; index < probe_errors.size(); ++index) {
      if (index != 0) {
        summary << "; ";
      }
      summary << probe_errors.at(index);
    }
    summary << ")";
  } else {
    summary << " (hardcoded ICS55 flow paths are unavailable)";
  }
  state.mode = RealTechMode::kSyntheticFallback;
  state.source_label = "synthetic_fallback";
  state.summary = summary.str();
  CTS_LOG_WARNING << "RealTechSetup: " << state.summary;
  return state;
}

}  // namespace icts_test::common::realtech::asset
