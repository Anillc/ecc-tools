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
#include <memory>
#include <unordered_map>
#include <vector>

#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
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

namespace icts {

#define WRAPPER_INST (icts::Wrapper::getInst())

class Wrapper
{
 public:
  static auto getInst() -> Wrapper&
  {
    static Wrapper inst;
    return inst;
  }

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
    _owned_pins.clear();
    _owned_insts.clear();
    _owned_nets.clear();
    _cts2idb_inst_map.clear();
    _idb2cts_inst_map.clear();
    _cts2idb_net_map.clear();
    _idb2cts_net_map.clear();
    _cts2idb_pin_map.clear();
    _idb2cts_pin_map.clear();
  }

  // Getter
  auto get_idb() const -> idb::IdbBuilder* { return _idb; }
  auto get_idb_design() const -> idb::IdbDesign* { return _idb_design; }
  auto get_idb_layout() const -> idb::IdbLayout* { return _idb_layout; }

  auto queryDbUnit() const -> int32_t;

  // Setter
  auto set_idb_design(idb::IdbDesign* design) -> void { _idb_design = design; }
  auto set_idb_layout(idb::IdbLayout* layout) -> void { _idb_layout = layout; }

  // Interface
  auto read() -> void;
  auto withinCore(int32_t point_x, int32_t point_y) const -> bool;

 private:
  Wrapper() = default;
  ~Wrapper() = default;

  // DB to CTS
  static auto idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>;
  auto idbToCts(idb::IdbPin* idb_pin) -> Pin*;
  auto idbToCts(idb::IdbInstance* idb_inst) -> Inst*;
  auto idbToCts(idb::IdbNet* idb_net) -> Net*;

  // CTS to DB
  static auto ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>;
  auto ctsToIdb(Pin* pin) -> idb::IdbPin*;
  auto ctsToIdb(Inst* inst) -> idb::IdbInstance*;
  auto ctsToIdb(Net* net) -> idb::IdbNet*;

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

  std::vector<std::unique_ptr<Pin>> _owned_pins;
  std::vector<std::unique_ptr<Inst>> _owned_insts;
  std::vector<std::unique_ptr<Net>> _owned_nets;

  std::unordered_map<Inst*, idb::IdbInstance*> _cts2idb_inst_map;
  std::unordered_map<idb::IdbInstance*, Inst*> _idb2cts_inst_map;

  std::unordered_map<Net*, idb::IdbNet*> _cts2idb_net_map;
  std::unordered_map<idb::IdbNet*, Net*> _idb2cts_net_map;

  std::unordered_map<Pin*, idb::IdbPin*> _cts2idb_pin_map;
  std::unordered_map<idb::IdbPin*, Pin*> _idb2cts_pin_map;
};

}  // namespace icts
