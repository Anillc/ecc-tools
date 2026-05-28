// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the
// Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace ieda_workspace {

using json = nlohmann::ordered_json;

struct PdkInfo
{
  std::string name;
  std::string version;
  std::string root;
  std::string tech_lef;
  std::vector<std::string> lef_paths;
  std::vector<std::string> lib_paths;
  std::string mapping_file;
  json corners = json::array();
  std::string sdc_path;
  std::string spef_path;
  std::string site_core;
  std::string site_io;
  std::string site_corner;
  std::string tap_cell;
  std::string end_cap;
  std::vector<std::string> buffers;
  std::vector<std::string> fillers;
  std::string tie_high_cell;
  std::string tie_high_port;
  std::string tie_low_cell;
  std::string tie_low_port;
};

struct WorkspaceStep
{
  std::string name;
  std::string tool;
  std::filesystem::path directory;
  std::map<std::string, std::string> config;
  std::map<std::string, std::string> input;
  std::map<std::string, std::string> output;
  std::map<std::string, std::string> data;
  std::map<std::string, std::string> feature;
  std::map<std::string, std::string> report;
  std::map<std::string, std::string> log;
  std::map<std::string, std::string> script;
  std::map<std::string, std::string> analysis;
};

struct Workspace
{
  std::filesystem::path directory;
  std::filesystem::path home_dir;
  std::filesystem::path home_json_path;
  std::filesystem::path parameters_path;
  std::filesystem::path flow_path;
  std::filesystem::path checklist_path;
  std::string design_name;
  std::string top_module;
  std::filesystem::path origin_def;
  std::filesystem::path origin_verilog;
  json home = json::object();
  json parameters = json::object();
  json flow = json::object();
  json checklist = json::object();
  PdkInfo pdk;
  std::vector<WorkspaceStep> steps;
};

class WorkspaceManager
{
 public:
  static WorkspaceManager& getInstance();

  bool load(const std::string& home_or_workspace_dir);
  bool prepare(bool force_rebuild_config = true);
  bool saveFlow() const;
  bool updateStepState(const std::string& step_name,
                       const std::string& tool,
                       const std::string& state,
                       const std::string& runtime = "",
                       int peak_memory_mb = -1);

  const Workspace& workspace() const { return _workspace; }
  const WorkspaceStep* getStep(const std::string& step_name) const;
  const WorkspaceStep* getStepByIndex(size_t index) const;
  size_t stepCount() const { return _workspace.steps.size(); }

  std::string getValue(const std::string& key) const;
  std::string getStepValue(const std::string& step_name, const std::string& key) const;
  std::string getStepName(size_t index) const;

 private:
  WorkspaceManager() = default;

  void reset();
  void resolveWorkspaceDir(const std::filesystem::path& home_or_workspace_dir);
  void loadHome();
  void loadParameters();
  void loadFlow();
  void loadChecklist();
  void buildPdk();
  void resolveOriginFiles();
  void buildSteps();
  void buildStep(WorkspaceStep& step,
                 const std::string& input_def,
                 const std::string& input_verilog,
                 const std::string& input_db) const;
  void createStepDirectories(const WorkspaceStep& step) const;
  bool hasStepConfigs(const WorkspaceStep& step) const;
  void writeStepConfigs(const WorkspaceStep& step) const;
  void writeDefaultSdcIfNeeded();

  std::string resolvePathValue(const std::string& path) const;
  std::string relativeToRoot(const std::string& path, const std::filesystem::path& root) const;
  std::string workspaceConfigPath(const std::string& path) const;
  std::string pdkConfigPath(const std::string& path) const;
  std::vector<std::string> pdkConfigPaths(const std::vector<std::string>& paths) const;
  std::string findOriginFile(const std::string& extension, const std::string& fallback_name) const;
  std::string scalarString(const json& value, const std::string& fallback = "") const;
  std::vector<std::string> stringArray(const json& value) const;
  double numberValue(const json& value, double fallback) const;
  int intValue(const json& value, int fallback) const;

  json defaultFlowConfig(const WorkspaceStep& step) const;
  json defaultDbConfig(const WorkspaceStep& step) const;
  json defaultFloorplanConfig() const;
  json defaultNoConfig(const WorkspaceStep& step) const;
  json defaultPlConfig() const;
  json defaultCtsConfig() const;
  json defaultRtConfig(const WorkspaceStep& step) const;
  json defaultDrcConfig() const;
  json defaultPnpConfig() const;
  json defaultToConfig(const std::string& type) const;
  json defaultRcxConfig(const WorkspaceStep& step) const;
  json buildSubflow(const std::string& step_name) const;
  json buildChecklist() const;

  Workspace _workspace;
};

}  // namespace ieda_workspace
