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
 * @file CTSSession.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CTSContext.hh"
#include "CTSState.hh"

namespace icts {

class CTSAPI;
class CtsCellLib;
class EvalNet;
class Net;
enum class LayerPattern;
template <typename T>
class CtsPoint;
using Point = CtsPoint<int>;

class CTSSession
{
 public:
  explicit CTSSession(CTSAPI& api);

  CTSSession(const CTSSession&) = delete;
  CTSSession(CTSSession&&) = delete;
  CTSSession& operator=(const CTSSession&) = delete;
  CTSSession& operator=(CTSSession&&) = delete;

  [[nodiscard]] CTSAPI& api() const;
  [[nodiscard]] CTSState& state();
  [[nodiscard]] const CTSState& state() const;
  [[nodiscard]] CTSContext buildContext() const;
  void installSourceRuntime() const;
  void clearSourceRuntime() const;

  [[nodiscard]] double getClockUnitCap(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const;
  [[nodiscard]] double getClockUnitRes(const std::optional<LayerPattern>& layer_pattern = std::nullopt) const;
  [[nodiscard]] double getSinkCap(const std::string& load_pin_full_name) const;
  [[nodiscard]] bool isClockNet(const std::string& net_name) const;
  [[nodiscard]] bool isTop(const std::string& net_name) const;
  [[nodiscard]] bool isInDie(const Point& point) const;
  [[nodiscard]] bool cellLibExist(const std::string& cell_master) const;
  [[nodiscard]] CtsCellLib* getCellLib(const std::string& cell_master) const;
  [[nodiscard]] std::vector<CtsCellLib*> getAllBufferLibs() const;
  [[nodiscard]] double getClockAT(const std::string& pin_name, const std::string& belong_clock_name) const;
  [[nodiscard]] std::string getCellType(const std::string& cell_master) const;
  [[nodiscard]] double getCellArea(const std::string& cell_master) const;
  [[nodiscard]] double getCellLeakagePower(const std::string& cell_master) const;
  [[nodiscard]] double getCellCap(const std::string& cell_master) const;
  [[nodiscard]] int32_t getDbUnit() const;
  [[nodiscard]] size_t genId() const;
  void registerSynthesisNet(Net* net) const;
  [[nodiscard]] Net* findSynthesisNet(const std::string& net_name) const;
  void clearSynthesisNets() const;
  void setPropagateClock() const;
  void convertDbToTimingEngine() const;
  void reportTiming() const;
  void refresh() const;
  void buildRCTree(const EvalNet& eval_net) const;
  void checkFile(const std::string& dir, const std::string& file_name, const std::string& suffix = ".rpt") const;
  void logTitle(const std::string& title) const;
  void saveToLog(const std::string& text) const;
  void saveToLog(const std::string& key, const std::string& value) const;
  void saveToLog(const std::string& key, int value) const;
  void saveToLog(const std::string& key, const char* value) const;
  void logEnd() const;
  void latencySkewLog() const;
  void utilizationLog() const;
  void slackLog() const;

 private:
  CTSAPI* _api = nullptr;
  std::shared_ptr<CTSState> _state;
};

}  // namespace icts
