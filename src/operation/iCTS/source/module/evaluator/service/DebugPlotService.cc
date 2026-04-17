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
 * @file DebugPlotService.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "service/DebugPlotService.hh"

#include <fstream>

#include "CtsDBWrapper.hh"
#include "GDSPloter.hh"
#include "context/EvaluatorRuntimeContext.hh"
#include "log/Log.hh"

namespace icts {

void DebugPlotService::plotPath(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets,
                                const std::string& inst_name, const std::string& file, int default_size) const
{
  auto* config = context.getConfig();
  auto* db_wrapper = context.getDbWrapper();
  auto path = config->get_work_dir() + "/" + file;
  auto ofs = std::fstream(path, std::ios::out | std::ios::trunc | std::ios::binary);

  CtsInstance* path_inst = nullptr;
  for (const auto& eval_net : eval_nets) {
    auto* inst = eval_net.get_instance(inst_name);
    if (inst != nullptr && eval_net.get_driver() != inst) {
      path_inst = inst;
      break;
    }
  }
  LOG_FATAL_IF(path_inst == nullptr) << "Cannot find instance: " << inst_name;

  GDSPloter::head(ofs);
  std::vector<CtsInstance*> insts;
  while (path_inst != nullptr) {
    GDSPloter::insertInstance(ofs, path_inst);
    insts.emplace_back(path_inst);
    auto* before_load_pin = path_inst->get_load_pin();
    if (before_load_pin == nullptr) {
      break;
    }

    auto* before_net = before_load_pin->get_net();
    auto* driver_pin = before_net->get_driver_pin();
    if (driver_pin == nullptr) {
      context.saveToLog("Net: ", before_net->get_net_name());
      context.saveToLog("Driver Pin: ", "<null>");
      break;
    }
    if (driver_pin->is_io() || !context.isClockNet(before_net->get_net_name())) {
      break;
    }

    context.saveToLog("Net: ", before_net->get_net_name());
    context.saveToLog("Driver Pin: ", driver_pin->get_full_name());
    for (auto* load_pin : before_net->get_load_pins()) {
      if (db_wrapper->ctsToIdb(load_pin)->is_flip_flop_clk()) {
        context.saveToLog("Load Clock Pin: ", load_pin->get_full_name());
      }
    }
    auto* driver_inst = driver_pin->get_instance();
    GDSPloter::insertWire(ofs, driver_pin->get_location(), before_load_pin->get_location());
    path_inst = driver_inst;
  }

  auto core = db_wrapper->get_core_bounding_box();
  GDSPloter::insertPolygon(ofs, core, "core", default_size);
  GDSPloter::strBegin(ofs);
  GDSPloter::topBegin(ofs);
  for (auto* inst : insts) {
    GDSPloter::refInstance(ofs, inst);
  }
  GDSPloter::refPolygon(ofs, "core");
  GDSPloter::refPolygon(ofs, "WIRE");
  GDSPloter::strEnd(ofs);
  GDSPloter::tail(ofs);
  LOG_INFO << "Path to " << inst_name << " has been written to " << path;
}

void DebugPlotService::plotNet(const EvaluatorRuntimeContextInterface& context, const std::string& net_name, const std::string& file,
                               int default_size) const
{
  auto* design = context.getDesign();
  auto& clk_nets = design->get_nets();

  CtsNet* net = nullptr;
  for (auto* clk_net : clk_nets) {
    if (clk_net->get_net_name() == net_name) {
      net = clk_net;
      break;
    }
  }
  LOG_FATAL_IF(net == nullptr) << "Net " << net_name << " not found";

  auto* config = context.getConfig();
  auto* db_wrapper = context.getDbWrapper();
  auto path = config->get_work_dir() + "/" + file;
  auto ofs = std::fstream(path, std::ios::out | std::ios::trunc | std::ios::binary);

  GDSPloter::head(ofs);
  auto insts = net->get_instances();
  for (auto* inst : insts) {
    GDSPloter::insertInstance(ofs, inst);
  }
  for (const auto& wire : net->get_signal_wires()) {
    auto first = wire.get_first().point;
    auto second = wire.get_second().point;
    GDSPloter::insertWire(ofs, first, second);
  }

  auto core = db_wrapper->get_core_bounding_box();
  GDSPloter::insertPolygon(ofs, core, "core", default_size);
  GDSPloter::strBegin(ofs);
  GDSPloter::topBegin(ofs);
  for (auto* inst : insts) {
    GDSPloter::refInstance(ofs, inst);
  }
  GDSPloter::refPolygon(ofs, "core");
  GDSPloter::refPolygon(ofs, "WIRE");
  GDSPloter::strEnd(ofs);
  GDSPloter::tail(ofs);
  LOG_INFO << "Net: " << net_name << " has been written to " << path;
}

}  // namespace icts
