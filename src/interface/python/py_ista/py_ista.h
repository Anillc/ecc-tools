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
#pragma once

#include <pybind11/numpy.h>
#include <pybind11/stl_bind.h>
#include <vector>
#include "netlist/DesignObject.hh"
#include "netlist/Pin.hh"
#include "netlist/Netlist.hh"
#include "timing_api.hh"
#include "sta/StaDelayPropagation.hh"
#include "sta/StaSlewPropagation.hh"
#include "Lib.hh"
#include <set>
#include <string>
#include <vector>

namespace python_interface {
bool staRun(const std::string& output);

bool staInit(const std::string& output);

bool staReport(const std::string& output);
bool setDesignWorkSpace(const std::string& design_workspace);

bool initLog(std::string log_path);

bool read_lef_def(std::vector<std::string>& lef_files, const std::string& def_file);
bool readVerilog(const std::string& file_name);

bool readLiberty(std::vector<std::string>& lib_files);

bool linkDesign(const std::string& cell_name);

bool readSpef(const std::string& file_name);

bool readSdc(const std::string& file_name);
bool reportTiming(int digits, const std::string& delay_type, std::set<std::string> exclude_cell_names, bool derate);
void build_timing_graph();
void update_clock_timing();
void buildRcTreeFromFlatData(
    const std::string& netName,
    const std::vector<std::string>& node_sta_names,
    const std::vector<bool>& node_is_pin, 
    const std::vector<int>& steiner_indices,
    const std::vector<int>& parent_indices,
    const std::vector<double>& node_total_caps,
    const std::vector<double>& edge_resistances,
    const std::vector<int>& node_global_indices);
void collectRctDataAndFillList(
    ista::RctNode* node,
    ista::Net* net,
    pybind11::list& results_list,
    std::unordered_set<ista::RctNode*>& visited);
void updateAndGetAllPinTimings(
  const std::vector<std::string>& pin_names,
  pybind11::list& arrival_late_times,
  pybind11::list& arrival_early_times,
  pybind11::list& required_late_times,
  pybind11::list& required_early_times,
  pybind11::list& pin_net_delay,
  pybind11::list& cell_arc_delays,
  pybind11::list& net_timing_details // 结果将直接填充到这里
);
void convertDBToTimingNetlist();
std::vector<std::string> get_used_libs();

}  // namespace python_interface