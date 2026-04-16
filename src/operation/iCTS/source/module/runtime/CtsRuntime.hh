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
 * @file CtsRuntime.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "log/Log.hh"

namespace icts {

class CtsConfig;
class CtsDBWrapper;
class CtsDesign;
class CtsCellLib;
class EvalNet;
class Net;
enum class LayerPattern;
template <typename T>
class CtsPoint;
using Point = CtsPoint<int>;

class CtsRuntimeInterface
{
 public:
  virtual ~CtsRuntimeInterface() = default;

  virtual CtsConfig* getConfig() const = 0;
  virtual CtsDesign* getDesign() const = 0;
  virtual CtsDBWrapper* getDbWrapper() const = 0;

  virtual double getClockUnitCap(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const = 0;
  virtual double getClockUnitRes(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const = 0;
  virtual double getSinkCap(const std::string& load_pin_full_name) const = 0;
  virtual bool isClockNet(const std::string& net_name) const = 0;
  virtual bool isTop(const std::string& net_name) const = 0;
  virtual bool isInDie(const Point& point) const = 0;

  virtual bool cellLibExist(const std::string& cell_master) const = 0;
  virtual CtsCellLib* getCellLib(const std::string& cell_master) const = 0;
  virtual std::vector<CtsCellLib*> getAllBufferLibs() const = 0;
  virtual double getClockAt(const std::string& pin_name, const std::string& belong_clock_name) const = 0;
  virtual std::string getCellType(const std::string& cell_master) const = 0;
  virtual double getCellArea(const std::string& cell_master) const = 0;
  virtual double getCellLeakagePower(const std::string& cell_master) const = 0;
  virtual double getCellCap(const std::string& cell_master) const = 0;

  virtual int32_t getDbUnit() const = 0;
  virtual size_t genId() = 0;
  virtual void registerSynthesisNet(Net* net) = 0;
  virtual Net* findSynthesisNet(const std::string& net_name) const = 0;
  virtual void clearSynthesisNets() = 0;

  virtual void setPropagateClock() const = 0;
  virtual void convertDbToTimingEngine() const = 0;
  virtual void reportTiming() const = 0;
  virtual void refresh() const = 0;
  virtual void buildRcTree(const EvalNet& eval_net) const = 0;

  virtual void checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix = ".rpt") const = 0;
  virtual void logTitle(const std::string& title) const = 0;
  virtual void saveToLog(const std::string& text) const = 0;
  virtual void logEnd() const = 0;
  virtual void latencySkewLog() const = 0;
  virtual void utilizationLog() const = 0;
  virtual void slackLog() const = 0;
};

class CtsRuntimeRegistry
{
 public:
  static void install(std::shared_ptr<CtsRuntimeInterface> runtime) { runtimeInstance() = std::move(runtime); }

  static void clear() { runtimeInstance().reset(); }

  static bool isInstalled() { return runtimeInstance() != nullptr; }

  static CtsRuntimeInterface& get()
  {
    auto& runtime = runtimeInstance();
    LOG_FATAL_IF(runtime == nullptr) << "CTS runtime is not installed.";
    return *runtime;
  }

 private:
  static std::shared_ptr<CtsRuntimeInterface>& runtimeInstance()
  {
    static std::shared_ptr<CtsRuntimeInterface> runtime;
    return runtime;
  }
};

inline CtsRuntimeInterface& GetCtsRuntime()
{
  return CtsRuntimeRegistry::get();
}

}  // namespace icts
