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
 * @file WrapperClockWriterInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Internal clock writeback helpers for the iCTS iDB wrapper
 */

#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace idb {
class IdbInstance;
class IdbNet;
class IdbPin;
}  // namespace idb

namespace icts {

class Clock;
class Net;

struct IdbNetPinSnapshot
{
  std::vector<idb::IdbPin*> io_pins;
  std::vector<idb::IdbPin*> inst_pins;
};

struct ClockIdbWriteScope
{
  std::set<std::string> touched_net_names;
  std::set<std::string> clock_tree_inst_names;
  std::unordered_map<const Clock*, std::vector<Net*>> reachable_nets_by_clock;
};

struct ClockIdbWriteBackup
{
  std::set<std::string> pre_existing_net_names;
  std::unordered_map<std::string, IdbNetPinSnapshot> net_pin_membership_by_name;
  std::set<std::string> pre_existing_inst_names;
};

auto AppendIdbPinToNet(idb::IdbNet* idb_net, idb::IdbPin* idb_pin) -> void;
auto ClearIdbNetPins(idb::IdbNet* idb_net) -> void;
auto FindIdbPinByTermOrPinName(idb::IdbInstance* idb_inst, const std::string& pin_name) -> idb::IdbPin*;
auto SnapshotIdbNetPins(idb::IdbNet* idb_net) -> IdbNetPinSnapshot;

}  // namespace icts
