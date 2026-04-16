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
 * @file RuntimeContextAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include <memory>

#include "CtsRuntime.hh"
#include "EvalNet.hh"
#include "context/EvaluatorRuntimeContext.hh"

namespace icts {
namespace {

class DefaultEvaluatorRuntimeContext : public EvaluatorRuntimeContextInterface
{
 public:
  CtsDesign* getDesign() const override { return GetCtsRuntime().getDesign(); }
  CtsConfig* getConfig() const override { return GetCtsRuntime().getConfig(); }
  CtsDBWrapper* getDbWrapper() const override { return GetCtsRuntime().getDbWrapper(); }

  bool isTop(const std::string& net_name) const override { return GetCtsRuntime().isTop(net_name); }
  bool isClockNet(const std::string& net_name) const override { return GetCtsRuntime().isClockNet(net_name); }
  bool cellLibExist(const std::string& cell_master) const override { return GetCtsRuntime().cellLibExist(cell_master); }
  std::string getCellType(const std::string& cell_master) const override { return GetCtsRuntime().getCellType(cell_master); }
  double getCellArea(const std::string& cell_master) const override { return GetCtsRuntime().getCellArea(cell_master); }
  double getCellCap(const std::string& cell_master) const override { return GetCtsRuntime().getCellCap(cell_master); }
  int getDbUnit() const override { return GetCtsRuntime().getDbUnit(); }
  Net* findSynthesisNet(const std::string& net_name) const override { return GetCtsRuntime().findSynthesisNet(net_name); }

  void setPropagateClock() const override { GetCtsRuntime().setPropagateClock(); }
  void refresh() const override { GetCtsRuntime().refresh(); }
  void buildRCTree(const EvalNet& eval_net) const override { GetCtsRuntime().buildRcTree(eval_net); }
  void reportTiming() const override { GetCtsRuntime().reportTiming(); }

  void checkFile(const std::string& dir, const std::string& file_name) const override { GetCtsRuntime().checkFile(dir, file_name); }
  void latencySkewLog() const override { GetCtsRuntime().latencySkewLog(); }
  void utilizationLog() const override { GetCtsRuntime().utilizationLog(); }
  void slackLog() const override { GetCtsRuntime().slackLog(); }

  void logTitle(const std::string& title) const override { GetCtsRuntime().logTitle(title); }
  void saveToLog(const std::string& text) const override { GetCtsRuntime().saveToLog(text); }
  void saveToLog(const std::string& key, const std::string& value) const override { GetCtsRuntime().saveToLog(key + value); }
  void saveToLog(const std::string& key, int value) const override { GetCtsRuntime().saveToLog(key + std::to_string(value)); }
  void saveToLog(const std::string& key, const char* value) const override
  {
    GetCtsRuntime().saveToLog(key + std::string(value == nullptr ? "" : value));
  }
  void logEnd() const override { GetCtsRuntime().logEnd(); }
};

}  // namespace

std::shared_ptr<EvaluatorRuntimeContextInterface> CreateDefaultEvaluatorRuntimeContext()
{
  return std::make_shared<DefaultEvaluatorRuntimeContext>();
}

}  // namespace icts
