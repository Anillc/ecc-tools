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
 * @file ClockTraceResolverInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Internal SDC clock trace resolver helper declarations.
 */

#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ClockTraceResolver.hh"
#include "SdcClockModel.hh"

struct RustLibertyExpr;

namespace idb {
class IdbDesign;
class IdbInstance;
class IdbNet;
class IdbPin;
}  // namespace idb

namespace ista {
class LibCell;
class LibPort;
}  // namespace ista

namespace icts::clock_trace {

inline constexpr std::size_t kTraceNetVisitLimit = 200000U;
inline constexpr std::size_t kTraceDepthLimit = 512U;

struct IdbNetPins
{
  idb::IdbPin* driver = nullptr;
  std::vector<idb::IdbPin*> loads;
  std::vector<idb::IdbPin*> all_pins;
};

struct ClockSinkStats
{
  std::size_t sequential_clock_sinks = 0U;
  std::size_t macro_clock_sinks = 0U;
};

struct TraceNode
{
  idb::IdbNet* net = nullptr;
  std::string path;
  std::size_t depth = 0U;
};

struct TraceTransition
{
  idb::IdbNet* net = nullptr;
  std::string path_step;
  std::string reason;
};

struct CaseConstraintSet
{
  std::set<std::string> pin_names;
  std::set<std::string> net_names;
};

struct ClockDeclView
{
  std::string clock_kind;
  std::string master_clock_name;
  std::set<std::string> sdc_target_net_names;
};

auto NetName(idb::IdbNet* net) -> std::string;
auto TermName(idb::IdbPin* pin) -> std::string;
auto PinFullName(idb::IdbPin* pin) -> std::string;
auto PinDisplayName(idb::IdbPin* pin) -> std::string;
auto CollectNetPins(idb::IdbNet* net) -> IdbNetPins;
auto IsInputLike(idb::IdbPin* pin) -> bool;
auto IsOutputLike(idb::IdbPin* pin) -> bool;
auto FindLibCell(idb::IdbInstance* inst) -> ista::LibCell*;
auto FindLibPort(ista::LibCell* lib_cell, idb::IdbPin* pin) -> ista::LibPort*;
auto IsSequentialCell(idb::IdbInstance* inst, ista::LibCell* lib_cell) -> bool;
auto IsClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool;
auto IsMacroClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool;
auto CountDirectClockSinks(idb::IdbNet* net) -> ClockSinkStats;
auto CountDirectClockSinksForReport(idb::IdbNet* net) -> ClockSinkStats;
auto IsClockTarget(const ClockSinkStats& stats) -> bool;
auto TargetKind(const ClockSinkStats& stats) -> std::string;
auto ClockKindName(const SdcClockDecl& clock) -> std::string;
auto MasterClockName(const SdcClockDecl& clock) -> std::string;
auto DominanceForRecord(const ClockTraceRecord& record, const std::string& clock_kind) -> std::string;
auto StrongTargetSinkThreshold() -> std::size_t;
auto IsStrongClockTarget(const ClockTraceRecord& record, std::size_t sink_threshold) -> bool;
auto FindInstPinByLibPort(idb::IdbInstance* inst, ista::LibPort* lib_port) -> idb::IdbPin*;
auto CollectOutputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>;
auto CollectInputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>;
auto IsCaseConstrained(idb::IdbPin* pin, const CaseConstraintSet& case_constraints) -> bool;
auto OtherInputsCaseConstrained(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, const CaseConstraintSet& case_constraints) -> bool;
auto LibertyExpressionUsesPort(RustLibertyExpr* expression, const std::string& port_name) -> bool;
auto OutputFunctionUsesInput(ista::LibCell* lib_cell, idb::IdbPin* output_pin, idb::IdbPin* input_pin) -> bool;
auto NetHasDirectClockSinks(idb::IdbNet* net) -> bool;
auto CountInputPinsOnNet(idb::IdbInstance* inst, idb::IdbNet* net) -> std::size_t;
auto CountClockTargetOutputs(const std::vector<idb::IdbPin*>& output_pins) -> std::size_t;
auto LibertyMarksClockInput(idb::IdbPin* input_pin, ista::LibCell* lib_cell) -> bool;
auto OtherInputsAreControlCandidates(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, ista::LibCell* lib_cell,
                                     const CaseConstraintSet& case_constraints) -> bool;
auto AddOutputTransition(std::vector<TraceTransition>& transitions, idb::IdbPin* output_pin, const std::string& reason) -> void;
auto CollectSafeTransitions(idb::IdbNet* net, const CaseConstraintSet& case_constraints) -> std::vector<TraceTransition>;

auto ObjectKindName(SdcObjectKind kind) -> std::string;
auto ResolvePortNet(idb::IdbDesign* idb_design, const std::string& port_name) -> idb::IdbNet*;
auto ResolvePinNet(idb::IdbDesign* idb_design, const std::string& pin_name) -> idb::IdbNet*;
auto ResolveRefNets(idb::IdbDesign* idb_design, const SdcObjectRef& ref) -> std::vector<idb::IdbNet*>;
auto BuildCaseConstraintSet(const SdcClockData& clock_data) -> CaseConstraintSet;
auto BuildGeneratedBoundaryOwners(idb::IdbDesign* idb_design, const SdcClockData& clock_data)
    -> std::unordered_map<idb::IdbNet*, std::string>;
auto TraceClock(idb::IdbDesign* idb_design, const SdcClockDecl& clock, const CaseConstraintSet& case_constraints,
                const std::unordered_map<idb::IdbNet*, std::string>& generated_boundary_owner_by_net) -> std::vector<ClockTraceRecord>;

auto JoinNames(const std::set<std::string>& names, std::size_t display_limit = 8U) -> std::string;
auto BuildClockDeclViews(idb::IdbDesign* idb_design, const SdcClockData& clock_data) -> std::map<std::string, ClockDeclView>;
auto AnnotateRecordOwnership(ClockTraceRecord& record, const std::map<std::string, ClockDeclView>& clock_view_by_name) -> void;
auto CollectTracedNetNames(const std::vector<ClockTraceRecord>& records) -> std::set<std::string>;
auto CollectUnownedClockLikeRecords(idb::IdbDesign* idb_design, const std::vector<ClockTraceRecord>& records)
    -> std::vector<ClockTraceRecord>;
auto NumberToString(std::size_t value) -> std::string;
auto EmitClockTraceReport(const std::vector<ClockTraceRecord>& records) -> void;
auto EmitSdcClockOwnershipReport(const SdcClockData& clock_data, const std::map<std::string, ClockDeclView>& clock_view_by_name,
                                 const std::vector<ClockTraceRecord>& records) -> void;
auto EmitUnownedClockLikeNetReport(const std::vector<ClockTraceRecord>& records) -> void;

}  // namespace icts::clock_trace
