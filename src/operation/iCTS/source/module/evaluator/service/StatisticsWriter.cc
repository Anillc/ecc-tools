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
 * @file StatisticsWriter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "service/StatisticsWriter.hh"

#include <fstream>
#include <sstream>

#include "CtsConfig.hh"
#include "Str.hh"
#include "context/EvaluatorRuntimeContext.hh"
#include "log/Log.hh"
#include "report/CtsReport.hh"
#include "time/Time.hh"

namespace icts {

namespace {

void mirrorToTerminalAndCtsLog(const EvaluatorRuntimeContextInterface& context, const std::string& text)
{
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    LOG_INFO << line;
  }
  context.saveToLog(text);
}

}  // namespace

void StatisticsWriter::pathLevelLog(const EvaluatorRuntimeContextInterface& context, const std::vector<PathInfo>& path_infos) const
{
  context.logTitle("Summary of Path Level");
  for (const auto& info : path_infos) {
    mirrorToTerminalAndCtsLog(context, "Root: " + info.root_name);
    mirrorToTerminalAndCtsLog(context, "\tClock Path Min num of Buffers: " + std::to_string(info.min_depth));
    mirrorToTerminalAndCtsLog(context, "\tClock Path Max num of Buffers: " + std::to_string(info.max_depth));
  }
  context.logEnd();
}

void StatisticsWriter::writeStatistics(const EvaluatorRuntimeContextInterface& context, const std::string& save_dir,
                                       const EvaluatorMetrics& metrics) const
{
  auto* config = context.getConfig();
  auto dir = (save_dir.empty() ? config->get_work_dir() : save_dir) + "/statistics";

  auto wl_rpt = CtsReportTable::createReportTable("Wire length stats", CtsReportType::kWireLength);
  (*wl_rpt) << "Top" << ieda::Str::printf("%.3f", metrics.top_wire_len) << TABLE_ENDLINE;
  (*wl_rpt) << "Trunk" << ieda::Str::printf("%.3f", metrics.trunk_wire_len) << TABLE_ENDLINE;
  (*wl_rpt) << "Leaf" << ieda::Str::printf("%.3f", metrics.leaf_wire_len) << TABLE_ENDLINE;
  (*wl_rpt) << "Total" << ieda::Str::printf("%.3f", metrics.total_wire_len) << TABLE_ENDLINE;
  (*wl_rpt) << "Max net length" << ieda::Str::printf("%.3f", metrics.max_net_len) << TABLE_ENDLINE;

  auto hpwl_wl_rpt = CtsReportTable::createReportTable("HPWL Wire length stats", CtsReportType::kHpWireLength);
  (*hpwl_wl_rpt) << "Top" << ieda::Str::printf("%.3f", metrics.hpwl_top_wire_len) << TABLE_ENDLINE;
  (*hpwl_wl_rpt) << "Trunk" << ieda::Str::printf("%.3f", metrics.hpwl_trunk_wire_len) << TABLE_ENDLINE;
  (*hpwl_wl_rpt) << "Leaf" << ieda::Str::printf("%.3f", metrics.hpwl_leaf_wire_len) << TABLE_ENDLINE;
  (*hpwl_wl_rpt) << "Total" << ieda::Str::printf("%.3f", metrics.hpwl_total_wire_len) << TABLE_ENDLINE;
  (*hpwl_wl_rpt) << "Max net length" << ieda::Str::printf("%.3f", metrics.hpwl_max_net_len) << TABLE_ENDLINE;

  auto wl_save_path = dir + "/wire_length.rpt";
  context.checkFile(dir, "wire_length");
  std::ofstream wl_save_file(wl_save_path);
  wl_save_file << "Generate the report at " << ieda::Time::getNowWallTime() << std::endl;
  wl_save_file << wl_rpt->c_str() << "\n\n";
  wl_save_file << hpwl_wl_rpt->c_str();

  auto cell_stats_rpt = CtsReportTable::createReportTable("Cell stats", CtsReportType::kCellStatus);
  for (const auto& [type, cell_property] : metrics.cell_stats_map) {
    (*cell_stats_rpt) << type << cell_property.total_num << cell_property.total_area << cell_property.total_cap << TABLE_ENDLINE;
  }
  auto cell_stats_save_path = dir + "/cell_stats.rpt";
  context.checkFile(dir, "cell_stats");
  std::ofstream cell_stats_save_file(cell_stats_save_path);
  cell_stats_save_file << "Generate the report at " << ieda::Time::getNowWallTime() << std::endl;
  cell_stats_save_file << cell_stats_rpt->c_str();

  auto lib_cell_dist_rpt = CtsReportTable::createReportTable("Library cell distribution", CtsReportType::kLibCellDist);
  for (const auto& [cell_master, count] : metrics.cell_dist_map) {
    (*lib_cell_dist_rpt) << cell_master << context.getCellType(cell_master) << count << count * context.getCellArea(cell_master)
                         << TABLE_ENDLINE;
  }
  auto lib_cell_dist_save_path = dir + "/lib_cell_dist.rpt";
  context.checkFile(dir, "lib_cell_dist");
  std::ofstream lib_cell_dist_save_file(lib_cell_dist_save_path);
  lib_cell_dist_save_file << "Generate the report at " << ieda::Time::getNowWallTime() << std::endl;
  lib_cell_dist_save_file << lib_cell_dist_rpt->c_str();

  auto net_level_rpt = CtsReportTable::createReportTable("Net level distribution", CtsReportType::kNetLevel);
  int all_num = 0;
  for (const auto& [_, num] : metrics.net_level_map) {
    all_num += num;
  }
  for (const auto& [level, num] : metrics.net_level_map) {
    (*net_level_rpt) << level << num << 1.0 * num / all_num << TABLE_ENDLINE;
  }
  auto net_level_save_path = dir + "/net_level.rpt";
  context.checkFile(dir, "net_level");
  std::ofstream net_level_save_file(net_level_save_path);
  net_level_save_file << "Generate the report at " << ieda::Time::getNowWallTime() << std::endl;
  net_level_save_file << net_level_rpt->c_str();

  context.latencySkewLog();
  context.utilizationLog();

  context.logTitle("Summary of Buffering (net)");
  mirrorToTerminalAndCtsLog(context, "--Cell Stats--");
  mirrorToTerminalAndCtsLog(context, cell_stats_rpt->c_str());
  if (metrics.cell_stats_map.empty()) {
    mirrorToTerminalAndCtsLog(context, "#No buffer is used#");
  }
  mirrorToTerminalAndCtsLog(context, "--Wirelength Stats--");
  mirrorToTerminalAndCtsLog(context, wl_rpt->c_str());
  context.logEnd();

  pathLevelLog(context, metrics.path_infos);
  context.slackLog();
}

}  // namespace icts
