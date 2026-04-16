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
 * @file EvaluatorRuntimeContext.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <memory>
#include <string>

namespace icts {

class CtsConfig;
class CtsDBWrapper;
class CtsDesign;
class EvalNet;
class Net;

class EvaluatorRuntimeContextInterface
{
 public:
  virtual ~EvaluatorRuntimeContextInterface() = default;

  virtual CtsDesign* getDesign() const = 0;
  virtual CtsConfig* getConfig() const = 0;
  virtual CtsDBWrapper* getDbWrapper() const = 0;

  virtual bool isTop(const std::string& net_name) const = 0;
  virtual bool isClockNet(const std::string& net_name) const = 0;
  virtual bool cellLibExist(const std::string& cell_master) const = 0;
  virtual std::string getCellType(const std::string& cell_master) const = 0;
  virtual double getCellArea(const std::string& cell_master) const = 0;
  virtual double getCellCap(const std::string& cell_master) const = 0;
  virtual int getDbUnit() const = 0;
  virtual Net* findSynthesisNet(const std::string& net_name) const = 0;

  virtual void setPropagateClock() const = 0;
  virtual void refresh() const = 0;
  virtual void buildRCTree(const EvalNet& eval_net) const = 0;
  virtual void reportTiming() const = 0;

  virtual void checkFile(const std::string& dir, const std::string& file_name) const = 0;
  virtual void latencySkewLog() const = 0;
  virtual void utilizationLog() const = 0;
  virtual void slackLog() const = 0;

  virtual void logTitle(const std::string& title) const = 0;
  virtual void saveToLog(const std::string& text) const = 0;
  virtual void saveToLog(const std::string& key, const std::string& value) const = 0;
  virtual void saveToLog(const std::string& key, int value) const = 0;
  virtual void saveToLog(const std::string& key, const char* value) const = 0;
  virtual void logEnd() const = 0;
};

std::shared_ptr<EvaluatorRuntimeContextInterface> CreateDefaultEvaluatorRuntimeContext();

}  // namespace icts
