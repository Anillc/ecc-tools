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
 * @file Wrapper.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief DB wrapper for iCTS
 */

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spatial/Point.hh"

namespace idb {
class IdbBuilder;
class IdbDesign;
class IdbInstance;
class IdbLayout;
class IdbNet;
class IdbPin;
template <typename T>
class IdbCoordinate;
}  // namespace idb

#include "logger/SchemaForward.hh"

namespace icts {

class Clock;
class Design;
class Inst;
class Net;
class Pin;
struct SdcClockTraceInput;
struct ClockTraceBuild;
struct ClockTraceClockTarget;
struct WrapperCellGeometry
{
  std::string name;
  std::string cell_master;
  Point<int> origin = Point<int>(-1, -1);
  int32_t width_dbu = 0;
  int32_t height_dbu = 0;
};

struct WrapperWriteSummary
{
  bool success = false;
  std::string failed_clock;
  std::string failed_net;
  bool idb_clock_tree_restored = false;
  std::string reason;
};

class Wrapper
{
 public:
  Wrapper() = default;
  ~Wrapper() = default;

  // Delete copy and move constructors
  Wrapper(const Wrapper& rhs) = delete;
  Wrapper(Wrapper&& rhs) = delete;
  auto operator=(const Wrapper& rhs) -> Wrapper& = delete;
  auto operator=(Wrapper&& rhs) -> Wrapper& = delete;

  // Initialize with idb builder
  auto init(idb::IdbBuilder* idb) -> void;

  // Reset wrapper
  auto reset() -> void
  {
    _idb = nullptr;
    _idb_design = nullptr;
    _idb_layout = nullptr;
    _cts2idb_inst_map.clear();
    _idb2cts_inst_map.clear();
    _cts2idb_net_map.clear();
    _idb2cts_net_map.clear();
    _cts2idb_pin_map.clear();
    _idb2cts_pin_map.clear();
  }

  auto queryDbUnit() const -> int32_t;
  auto is_design_ready() const -> bool { return _idb_design != nullptr; }
  auto is_layout_ready() const -> bool { return _idb != nullptr && _idb_layout != nullptr; }

  // Setter
  auto set_idb_design(idb::IdbDesign* design) -> void { _idb_design = design; }
  auto set_idb_layout(idb::IdbLayout* layout) -> void { _idb_layout = layout; }

  // Interface
  auto read(Design& design, SchemaWriter& reporter) -> void;
  auto readClocks(Design& design, SchemaWriter& reporter, const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> bool;
  auto readTraceClockTargets(Design& design, SchemaWriter& reporter, const std::vector<ClockTraceClockTarget>& clock_targets) -> bool;
  auto traceSdcClocks(const SdcClockTraceInput& input) const -> ClockTraceBuild;
  auto writeClock(Design& design, SchemaWriter& reporter, Clock& clock) -> bool;
  auto writeClocksDetailed(Design& design, SchemaWriter& reporter, const std::vector<Clock*>& clocks) -> WrapperWriteSummary;
  auto writeClocks(Design& design, SchemaWriter& reporter, const std::vector<Clock*>& clocks) -> bool;
  auto collectLogicCellGeometries() const -> std::vector<WrapperCellGeometry>;
  auto queryInstGeometry(const std::string& inst_name) const -> std::optional<WrapperCellGeometry>;
  auto withinCore(int32_t point_x, int32_t point_y) const -> bool;

 private:
  class CtsClockReader;
  class CtsClockIdbWriter;
  friend class CtsClockReader;
  friend class CtsClockIdbWriter;

  // DB to CTS
  static auto idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>;

  // CTS to DB
  static auto ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>;

  // cross reference
  auto crossRef(idb::IdbPin* idb_pin, Pin* cts_pin) -> void
  {
    _idb2cts_pin_map[idb_pin] = cts_pin;
    _cts2idb_pin_map[cts_pin] = idb_pin;
  }
  auto crossRef(idb::IdbInstance* idb_inst, Inst* cts_inst) -> void
  {
    _idb2cts_inst_map[idb_inst] = cts_inst;
    _cts2idb_inst_map[cts_inst] = idb_inst;
  }
  auto crossRef(idb::IdbNet* idb_net, Net* cts_net) -> void
  {
    _idb2cts_net_map[idb_net] = cts_net;
    _cts2idb_net_map[cts_net] = idb_net;
  }

  idb::IdbBuilder* _idb = nullptr;
  idb::IdbDesign* _idb_design = nullptr;
  idb::IdbLayout* _idb_layout = nullptr;

  std::unordered_map<Inst*, idb::IdbInstance*> _cts2idb_inst_map;
  std::unordered_map<idb::IdbInstance*, Inst*> _idb2cts_inst_map;

  std::unordered_map<Net*, idb::IdbNet*> _cts2idb_net_map;
  std::unordered_map<idb::IdbNet*, Net*> _idb2cts_net_map;

  std::unordered_map<Pin*, idb::IdbPin*> _cts2idb_pin_map;
  std::unordered_map<idb::IdbPin*, Pin*> _idb2cts_pin_map;
};

}  // namespace icts
