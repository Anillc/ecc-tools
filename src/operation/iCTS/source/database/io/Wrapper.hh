// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
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
#include <unordered_map>

#include "../design/Inst.hh"
#include "../design/Net.hh"
#include "../design/Pin.hh"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "builder.h"

namespace icts {

#define CTSWrapperInst (icts::Wrapper::getInst())

class Wrapper
{
 public:
  static Wrapper& getInst()
  {
    static Wrapper instance;
    return instance;
  }

  // Delete copy and move constructors
  Wrapper(const Wrapper& rhs) = delete;
  Wrapper(Wrapper&& rhs) = delete;
  Wrapper& operator=(const Wrapper& rhs) = delete;
  Wrapper& operator=(Wrapper&& rhs) = delete;

  // Initialize with idb builder
  void init(idb::IdbBuilder* idb)
  {
    _idb = idb;
    _idb_design = _idb->get_def_service()->get_design();
    _idb_layout = _idb->get_lef_service()->get_layout();
  }

  // Reset wrapper
  void reset()
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

  // Getter
  idb::IdbBuilder* get_idb() const { return _idb; }
  idb::IdbDesign* get_idb_design() const { return _idb_design; }
  idb::IdbLayout* get_idb_layout() const { return _idb_layout; }

  int32_t getDbUnit() const { return _idb_design->get_units()->get_micron_dbu(); }

  // Setter
  void set_idb(idb::IdbBuilder* idb)
  {
    _idb = idb;
    _idb_design = _idb->get_def_service()->get_design();
    _idb_layout = _idb->get_lef_service()->get_layout();
  }
  void set_idb_design(idb::IdbDesign* design) { _idb_design = design; }
  void set_idb_layout(idb::IdbLayout* layout) { _idb_layout = layout; }

  // Interface
  void read();

 private:
  Wrapper() = default;
  ~Wrapper() = default;

  // DB to CTS
  Point<int> idbToCts(idb::IdbCoordinate<int32_t>& coord) const;
  PinType idbToCts(idb::IdbConnectType idb_pin_type, idb::IdbConnectDirection idb_pin_direction) const;
  Pin* idbToCts(idb::IdbPin* idb_pin);
  Inst* idbToCts(idb::IdbInstance* idb_inst);
  Net* idbToCts(idb::IdbNet* idb_net);

  // CTS to DB
  idb::IdbCoordinate<int32_t> ctsToIdb(const Point<int>& loc) const;
  idb::IdbPin* ctsToIdb(Pin* pin);
  idb::IdbInstance* ctsToIdb(Inst* inst);
  idb::IdbNet* ctsToIdb(Net* net);

  // cross reference
  void crossRef(idb::IdbPin* idb_pin, Pin* cts_pin)
  {
    _idb2cts_pin_map[idb_pin] = cts_pin;
    _cts2idb_pin_map[cts_pin] = idb_pin;
  }
  void crossRef(idb::IdbInstance* idb_inst, Inst* cts_inst)
  {
    _idb2cts_inst_map[idb_inst] = cts_inst;
    _cts2idb_inst_map[cts_inst] = idb_inst;
  }
  void crossRef(idb::IdbNet* idb_net, Net* cts_net)
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
