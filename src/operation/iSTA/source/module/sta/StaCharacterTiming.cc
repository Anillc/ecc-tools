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
 * @file StaCharacterTiming.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief extract the timing model of design.
 * @version 0.1
 * @date 2024-03-12
 *
 */
#include "StaCharacterTiming.hh"

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "StaCheck.hh"
#include "StaDataPropagation.hh"
#include "StaDelayPropagation.hh"
#include "StaSlewPropagation.hh"
#include "ThreadPool/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <sstream>
#include <tuple>

namespace ista {

namespace {

bool isCharacterizationData(const StaData* data, uint64_t epoch) {
  return data && data->get_data_epoch() == epoch;
}

bool checkArcMatchesForTimingModel(const StaArc* lhs, const StaArc* rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }

  const bool lhs_setup_like = lhs->isSetupArc() || lhs->isRecoveryArc();
  const bool rhs_setup_like = rhs->isSetupArc() || rhs->isRecoveryArc();
  const bool lhs_hold_like = lhs->isHoldArc() || lhs->isRemovalArc();
  const bool rhs_hold_like = rhs->isHoldArc() || rhs->isRemovalArc();

  return lhs_setup_like == rhs_setup_like &&
         lhs_hold_like == rhs_hold_like &&
         lhs->isRisingEdgeCheck() == rhs->isRisingEdgeCheck();
}

struct BoundaryDelayTerms {
  StaVertex* start_vertex = nullptr;
  StaVertex* end_vertex = nullptr;
  TransType start_trans_type = TransType::kRise;
  TransType end_trans_type = TransType::kRise;
  int64_t start_arrive_fs = 0;
  int64_t end_arrive_fs = 0;
  int64_t delay_fs = 0;
};

struct BoundaryDelayCandidate {
  StaPathDelayData* delay_data = nullptr;
  BoundaryDelayTerms terms;
};

struct PathDelayBreakdownNs {
  double total_ns = 0.0;
  double cell_ns = 0.0;
  double net_ns = 0.0;
};

template <typename T>
std::optional<BoundaryDelayTerms> extractBoundaryDelayTerms(
    std::stack<T*> path_stack) {
  std::vector<T*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = path_stack.top();
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  auto* start_node = path_nodes.front();
  auto* end_node = path_nodes.back();
  BoundaryDelayTerms terms;
  terms.start_vertex = start_node->get_own_vertex();
  terms.end_vertex = end_node->get_own_vertex();
  terms.start_trans_type = start_node->get_trans_type();
  terms.end_trans_type = end_node->get_trans_type();
  terms.start_arrive_fs = start_node->get_arrive_time();
  terms.end_arrive_fs = end_node->get_arrive_time();
  terms.delay_fs = terms.end_arrive_fs - terms.start_arrive_fs;
  return terms;
}

std::optional<PathDelayBreakdownNs> rebuildPathDelayBreakdownNsForEpoch(
    StaPathDelayData* delay_data, uint64_t epoch) {
  if (!delay_data) {
    return std::nullopt;
  }

  auto path_stack = delay_data->getPathData();
  std::vector<StaPathDelayData*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = dynamic_cast<StaPathDelayData*>(path_stack.top());
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  PathDelayBreakdownNs breakdown;
  for (size_t i = 1; i < path_nodes.size(); ++i) {
    auto* src_vertex = path_nodes[i - 1]->get_own_vertex();
    auto* snk_node = path_nodes[i];
    auto* snk_vertex = snk_node->get_own_vertex();
    if (!src_vertex || !snk_vertex) {
      return std::nullopt;
    }

    auto arcs = snk_vertex->getSrcArc(src_vertex);
    StaArc* matched_arc = nullptr;
    for (auto* arc : arcs) {
      if (arc && arc->isDelayArc()) {
        matched_arc = arc;
        break;
      }
    }

    if (!matched_arc) {
      return std::nullopt;
    }

    const double arc_delay_ns =
        FS_TO_NS(matched_arc->get_arc_delay(snk_node->get_delay_type(),
                                            snk_node->get_trans_type(), epoch));
    breakdown.total_ns += arc_delay_ns;
    if (matched_arc->isNetArc()) {
      breakdown.net_ns += arc_delay_ns;
    } else {
      breakdown.cell_ns += arc_delay_ns;
    }
  }

  return breakdown;
}

std::vector<BoundaryDelayCandidate> collectBoundaryDelayCandidatesForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, uint64_t epoch,
    StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt,
    std::optional<TransType> end_trans_type = std::nullopt) {
  std::vector<BoundaryDelayCandidate> collected_terms;
  if (!vertex) {
    return collected_terms;
  }

  StaData* data = nullptr;
  FOREACH_DELAY_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* path_delay_data = dynamic_cast<StaPathDelayData*>(data);
    if (!path_delay_data) {
      continue;
    }

    auto delay_terms = extractBoundaryDelayTerms(path_delay_data->getPathData());
    if (!delay_terms) {
      continue;
    }

    if (start_vertex && delay_terms->start_vertex != start_vertex) {
      continue;
    }
    if (start_trans_type &&
        delay_terms->start_trans_type != *start_trans_type) {
      continue;
    }
    if (end_trans_type && delay_terms->end_trans_type != *end_trans_type) {
      continue;
    }

    collected_terms.push_back({path_delay_data, *delay_terms});
  }

  return collected_terms;
}

std::vector<BoundaryDelayTerms> collectBoundaryDelayTermsForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, uint64_t epoch,
    StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt,
    std::optional<TransType> end_trans_type = std::nullopt) {
  std::vector<BoundaryDelayTerms> collected_terms;
  for (const auto& candidate : collectBoundaryDelayCandidatesForEpoch(
           vertex, analysis_mode, epoch, start_vertex, start_trans_type,
           end_trans_type)) {
    collected_terms.push_back(candidate.terms);
  }
  return collected_terms;
}

std::map<StaVertex*, StaPathDelayData*> getDifferentStartPathDelayDataForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, TransType trans_type,
    uint64_t epoch) {
  using PathDataQueue = std::priority_queue<StaData*, std::vector<StaData*>,
                                            decltype(sta_data_cmp)>;
  std::map<StaVertex*, PathDataQueue> start_vertex_path_data_queue;

  StaData* data = nullptr;
  FOREACH_DELAY_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* path_delay_data = dynamic_cast<StaPathDelayData*>(data);
    if (!path_delay_data) {
      continue;
    }

    auto path_data = path_delay_data->getPathData();
    if (path_data.empty()) {
      continue;
    }

    auto* path_start_vertex = path_data.top()->get_own_vertex();
    auto [queue_iter, inserted] = start_vertex_path_data_queue.emplace(
        path_start_vertex, PathDataQueue(sta_data_cmp));
    queue_iter->second.push(data);
  }

  std::map<StaVertex*, StaPathDelayData*> start_vertex_to_worst_data;
  for (auto& [start_vertex, data_queue] : start_vertex_path_data_queue) {
    if (!data_queue.empty()) {
      start_vertex_to_worst_data[start_vertex] =
          dynamic_cast<StaPathDelayData*>(data_queue.top());
    }
  }

  return start_vertex_to_worst_data;
}

std::optional<double> findVertexSlewNsForEpoch(StaVertex* vertex,
                                               AnalysisMode analysis_mode,
                                               TransType trans_type,
                                               uint64_t epoch) {
  if (!vertex) {
    return std::nullopt;
  }

  StaSlewData* selected = nullptr;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (!slew_data) {
      continue;
    }

    if (!selected) {
      selected = slew_data;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         slew_data->get_slew() > selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         slew_data->get_slew() < selected->get_slew())) {
      selected = slew_data;
    }
  }

  return selected ? std::optional<double>(FS_TO_NS(selected->get_slew()))
                  : std::nullopt;
}

std::optional<double> findVertexSlewNsForEpochWithStart(
    StaVertex* vertex, AnalysisMode analysis_mode, TransType trans_type,
    uint64_t epoch, StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt) {
  if (!vertex) {
    return std::nullopt;
  }

  StaSlewData* selected = nullptr;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (!slew_data) {
      continue;
    }

    if (start_vertex || start_trans_type) {
      auto path_data = slew_data->getPathData();
      if (path_data.empty()) {
        continue;
      }

      auto* path_start_data = path_data.top();
      if (!path_start_data) {
        continue;
      }

      if (start_vertex && path_start_data->get_own_vertex() != start_vertex) {
        continue;
      }
      if (start_trans_type &&
          path_start_data->get_trans_type() != *start_trans_type) {
        continue;
      }
    }

    if (!selected) {
      selected = slew_data;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         slew_data->get_slew() > selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         slew_data->get_slew() < selected->get_slew())) {
      selected = slew_data;
    }
  }

  return selected ? std::optional<double>(FS_TO_NS(selected->get_slew()))
                  : std::nullopt;
}

StaSlewData* findWorstVertexSlewDataForEpoch(StaVertex* vertex,
                                             AnalysisMode analysis_mode,
                                             TransType trans_type,
                                             uint64_t epoch,
                                             StaVertex* start_vertex =
                                                 nullptr) {
  if (!vertex) {
    return nullptr;
  }

  std::priority_queue<StaData*, std::vector<StaData*>, decltype(sta_data_cmp)>
      data_queue(sta_data_cmp);

  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    if (start_vertex) {
      auto path_data = data->getPathData();
      if (path_data.empty() || path_data.top()->get_own_vertex() != start_vertex) {
        continue;
      }
    }

    data_queue.push(data);
  }

  if (data_queue.empty()) {
    return nullptr;
  }

  return dynamic_cast<StaSlewData*>(data_queue.top());
}

std::vector<const PreservedSeqCheckSnapshot*> findSeqCheckCandidatesForPort(
    const std::vector<PreservedSeqCheckSnapshot>& seq_check_snapshots,
    StaVertex* clk_point, StaArc* check_arc, AnalysisMode capture_analysis_mode,
    TransType input_trans_type, TransType clock_trans_type) {
  std::vector<const PreservedSeqCheckSnapshot*> matched_seq_data;
  if (!clk_point) {
    return matched_seq_data;
  }

  for (const auto& seq_snapshot : seq_check_snapshots) {
    if (check_arc &&
        !checkArcMatchesForTimingModel(seq_snapshot.check_arc, check_arc)) {
      continue;
    }

    if (!seq_snapshot.capture_clock_vertex ||
        seq_snapshot.capture_clock_vertex != clk_point ||
        seq_snapshot.capture_analysis_mode != capture_analysis_mode ||
        seq_snapshot.clock_trans_type != clock_trans_type) {
      continue;
    }

    if (seq_snapshot.data_start_trans_type != input_trans_type) {
      continue;
    }

    matched_seq_data.push_back(&seq_snapshot);
  }

  return matched_seq_data;
}

double convertPfToReferenceCapUnit(LibArc* reference_arc, double cap_pf) {
  auto* owner_cell = reference_arc ? reference_arc->get_owner_cell() : nullptr;
  auto* owner_lib = owner_cell ? owner_cell->get_owner_lib() : nullptr;
  return owner_lib
             ? ConvertCapUnit(CapacitiveUnit::kPF, owner_lib->get_cap_unit(),
                              cap_pf)
             : cap_pf;
}

double convertNsToReferenceTimeAxisUnit(LibArc* reference_arc,
                                        double time_ns) {
  auto* owner_cell = reference_arc ? reference_arc->get_owner_cell() : nullptr;
  auto* owner_lib = owner_cell ? owner_cell->get_owner_lib() : nullptr;
  if (!owner_lib) {
    return time_ns;
  }

  switch (owner_lib->get_time_unit()) {
    case TimeUnit::kPS:
      return time_ns * 1e3;
    case TimeUnit::kFS:
      return time_ns * 1e6;
    case TimeUnit::kNS:
    default:
      return time_ns;
  }
}

bool shouldTraceCharacterizationPin(const char* pin_name) {
  if (!pin_name) {
    return false;
  }

  const char* trace_env = std::getenv("IEDA_CHARACTER_TIMING_TRACE_PINS");
  if (!trace_env || !*trace_env) {
    return false;
  }

  std::stringstream ss(trace_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(), ::isspace),
               item.end());
    if (!item.empty() && item == pin_name) {
      return true;
    }
  }

  return false;
}

bool shouldTraceClockSlewVertex(const std::string& vertex_name) {
  const char* trace_env = std::getenv("IEDA_TRACE_CLOCK_SLEW_VERTICES");
  if (!trace_env || !*trace_env) {
    return false;
  }

  std::stringstream ss(trace_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(), ::isspace),
               item.end());
    if (!item.empty() && vertex_name.find(item) != std::string::npos) {
      return true;
    }
  }

  return false;
}

const char* analysisModeName(AnalysisMode analysis_mode) {
  switch (analysis_mode) {
    case AnalysisMode::kMax:
      return "max";
    case AnalysisMode::kMin:
      return "min";
    case AnalysisMode::kMaxMin:
      return "maxmin";
    default:
      return "unknown";
  }
}

const char* transTypeName(TransType trans_type) {
  switch (trans_type) {
    case TransType::kRise:
      return "rise";
    case TransType::kFall:
      return "fall";
    default:
      return "unknown";
  }
}

std::string formatOptionalNs(std::optional<double> value) {
  if (!value) {
    return "na";
  }

  std::ostringstream os;
  os << *value;
  return os.str();
}

std::string buildSlewCandidateTrace(StaVertex* the_vertex,
                                    AnalysisMode analysis_mode,
                                    TransType trans_type) {
  std::vector<StaSlewData*> candidates;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(the_vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (slew_data) {
      candidates.push_back(slew_data);
    }
  }

  std::ostringstream os;
  const auto field_prefix =
      std::string(analysisModeName(analysis_mode)) + "_" +
      transTypeName(trans_type);
  os << " " << field_prefix << "_candidate_count=" << candidates.size();

  auto* api_selected = the_vertex->getWorstSlewData(analysis_mode, trans_type);
  os << " " << field_prefix
     << "_api_selected_slew_ns="
     << formatOptionalNs(api_selected
                             ? std::optional<double>(
                                   FS_TO_NS(api_selected->get_slew()))
                             : std::nullopt);

  StaSlewData* manual_selected = nullptr;
  for (auto* candidate : candidates) {
    if (!manual_selected) {
      manual_selected = candidate;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         candidate->get_slew() > manual_selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         candidate->get_slew() < manual_selected->get_slew())) {
      manual_selected = candidate;
    }
  }

  os << " " << field_prefix
     << "_manual_selected_slew_ns="
     << formatOptionalNs(manual_selected
                             ? std::optional<double>(
                                   FS_TO_NS(manual_selected->get_slew()))
                             : std::nullopt);

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](StaSlewData* left, StaSlewData* right) {
                     if (left->get_slew() != right->get_slew()) {
                       return left->get_slew() < right->get_slew();
                     }
                     if (left->get_data_epoch() != right->get_data_epoch()) {
                       return left->get_data_epoch() < right->get_data_epoch();
                     }

                     auto* left_bwd_vertex =
                         left->get_bwd() ? left->get_bwd()->get_own_vertex()
                                         : nullptr;
                     auto* right_bwd_vertex =
                         right->get_bwd() ? right->get_bwd()->get_own_vertex()
                                          : nullptr;
                     const auto left_name =
                         left_bwd_vertex ? left_bwd_vertex->getName() : "";
                     const auto right_name =
                         right_bwd_vertex ? right_bwd_vertex->getName() : "";
                     return left_name < right_name;
                   });

  os << " " << field_prefix << "_candidates=[";
  for (size_t i = 0; i < candidates.size(); ++i) {
    auto* candidate = candidates[i];
    auto* bwd_vertex =
        candidate->get_bwd() ? candidate->get_bwd()->get_own_vertex() : nullptr;
    if (i != 0) {
      os << ";";
    }
    os << "{slew_ns=" << FS_TO_NS(candidate->get_slew())
       << ",epoch=" << candidate->get_data_epoch()
       << ",bwd=" << (bwd_vertex ? bwd_vertex->getName() : "null") << "}";
  }
  os << "]";

  return os.str();
}

void traceClockSlewVertexSnapshot(StaVertex* the_vertex, bool snapshot_eligible,
                                  size_t snapshot_entries) {
  if (!the_vertex ||
      !shouldTraceClockSlewVertex(the_vertex->getName())) {
    return;
  }

  std::ostringstream os;
  os << "[character_timing][full-sta-clock-snapshot] vertex="
     << the_vertex->getName() << " is_port=" << the_vertex->is_port()
     << " is_start=" << the_vertex->is_start()
     << " is_clock=" << the_vertex->is_clock()
     << " is_sdc_clock_pin=" << the_vertex->is_sdc_clock_pin()
     << " snapshot_eligible=" << snapshot_eligible
     << " snapshot_entries=" << snapshot_entries;

  for (auto analysis_mode : {AnalysisMode::kMax, AnalysisMode::kMin}) {
    for (auto trans_type : {TransType::kRise, TransType::kFall}) {
      auto* slew_data = the_vertex->getWorstSlewData(analysis_mode, trans_type);
      auto slew_ns =
          slew_data ? std::optional<double>(FS_TO_NS(slew_data->get_slew()))
                    : std::nullopt;
      auto clock_datas = the_vertex->getClockData(analysis_mode, trans_type);
      auto clock_arrive_ns =
          the_vertex->getClockArriveTime(analysis_mode, trans_type);

      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_slew_ns=";
      if (slew_ns) {
        os << *slew_ns;
      } else {
        os << "na";
      }

      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_clock_data="
         << clock_datas.size();
      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_clock_arrive_ns=";
      if (clock_arrive_ns) {
        os << FS_TO_NS(*clock_arrive_ns);
      } else {
        os << "na";
      }

      os << buildSlewCandidateTrace(the_vertex, analysis_mode, trans_type);
    }
  }

  LOG_INFO << os.str();
}

std::optional<double> rebuildPathDelayNsForEpoch(StaPathDelayData* delay_data,
                                                 uint64_t epoch) {
  if (!delay_data) {
    return std::nullopt;
  }

  auto path_stack = delay_data->getPathData();
  std::vector<StaPathDelayData*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = dynamic_cast<StaPathDelayData*>(path_stack.top());
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  int64_t rebuilt_delay_fs = 0;
  for (size_t i = 1; i < path_nodes.size(); ++i) {
    auto* src_vertex = path_nodes[i - 1]->get_own_vertex();
    auto* snk_node = path_nodes[i];
    auto* snk_vertex = snk_node->get_own_vertex();
    if (!src_vertex || !snk_vertex) {
      return std::nullopt;
    }

    auto arcs = snk_vertex->getSrcArc(src_vertex);
    StaArc* matched_arc = nullptr;
    for (auto* arc : arcs) {
      if (arc && arc->isDelayArc()) {
        matched_arc = arc;
        break;
      }
    }

    if (!matched_arc) {
      return std::nullopt;
    }

    rebuilt_delay_fs += matched_arc->get_arc_delay(
        snk_node->get_delay_type(), snk_node->get_trans_type(), epoch);
  }

  if (auto* launch_clock_data = delay_data->get_launch_clock_data();
      launch_clock_data) {
    rebuilt_delay_fs += launch_clock_data->get_arrive_time();
  }

  return FS_TO_NS(rebuilt_delay_fs);
}

void resetCharacterizationPayload(StaGraph* the_graph) {
  auto reset_vertex_payload = [](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    // Preserve clock/context buckets from the propagated-clock refresh, but
    // drop full-STA local payload so ETM extraction seeds a clean epoch-local
    // graph.
    the_vertex->resetSlewBucket();
    the_vertex->resetPathDelayBucket();
    the_vertex->resetColor();
    the_vertex->resetLevel();
    the_vertex->reset_is_slew_prop();
    the_vertex->reset_is_delay_prop();
    the_vertex->reset_is_fwd();
    the_vertex->reset_is_bwd();
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    reset_vertex_payload(the_vertex);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    reset_vertex_payload(assistant_vertex.get());
  }

  StaArc* the_arc = nullptr;
  FOREACH_ARC(the_graph, the_arc) {
    the_arc->resetArcDelayBucket();
    if (auto* net_arc = dynamic_cast<StaNetArc*>(the_arc)) {
      net_arc->getWaveformBucket().freeData();
    }
  }
}

}  // namespace

uint64_t StaCharacterTiming::nextCharacterizationEpoch() {
  static std::atomic<uint64_t> next_epoch{1};
  return next_epoch.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief propagate from the vertex.
 *
 * @param the_vertex
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaVertex* the_vertex) {
  if (_state == kCollectEndpoints || _state == kBackPropagateRTToPort) {
    if ((_state == kBackPropagateRTToPort) && (the_vertex->is_bwd())) {
      return 1;
    }

    if (the_vertex->is_end()) {
      if (_state == kCollectEndpoints) {
        // collect the interface logic endpoint.
        if (!the_vertex->is_port()) {
          _port_to_logic_endpoint.insert(_current_port_vertex, the_vertex);
        } else {
          _output_port_to_input_port.insert(the_vertex, _current_port_vertex);
        }

        _interface_logic_endpoints.insert(the_vertex);

      } else if (_state == kBackPropagateRTToPort) {
        // set the constrain require time.
        auto set_constain_value = [the_vertex](auto analysis_mode,
                                               auto trans_type) {
          if (!the_vertex->is_port()) {
            // get the delay value from the check arc(delay arc)
            auto* check_arc = the_vertex->getCheckArc(analysis_mode);
            auto constrain_val =
                check_arc->get_arc_delay(analysis_mode, trans_type);
            the_vertex->setConstainTime(analysis_mode, trans_type,
                                        constrain_val);
          } else {
            the_vertex->setConstainTime(analysis_mode, trans_type, 0);
          }
        };

        FOREACH_MODE_TRANS(analysis_mode, trans_type) {
          set_constain_value(analysis_mode, trans_type);
        }
      }

      return 1;
    }

    if (the_vertex->is_clock()) {
      if (_state == kCollectEndpoints) {
        LOG_FATAL_IF(!_current_port_vertex)
            << "current port vertex is null when clock vertex "
            << the_vertex->getName() << " is reached.";
        _logic_clkpoint_to_port.insert(the_vertex, _current_port_vertex);
        _port_to_logic_clkpoint.insert(_current_port_vertex, the_vertex);
        _interface_logic_endpoints.insert(the_vertex);
      }

      return 1;
    }

    // fwd
    FOREACH_SRC_ARC(the_vertex, src_arc) {
      if (src_arc->isMpwArc()) {
        continue;
      }

      (*this)(src_arc);
      (*this)(src_arc->get_snk());
    }

    if (_state == kBackPropagateRTToPort) {
      the_vertex->set_is_bwd();
    }

  } else if (_state == kPropagateSlew || _state == kPropagateDelay ||
             _state == kPropagateATFromPort) {
    const bool characterize_clock_path =
        _current_logic_endpoint && _current_logic_endpoint->is_clock();
    if ((_state == kPropagateSlew) && (the_vertex->is_slew_prop())) {
      return 1;
    } else if ((_state == kPropagateDelay) && (the_vertex->is_delay_prop())) {
      return 1;
    } else if ((_state == kPropagateATFromPort) && (the_vertex->is_fwd())) {
      return 1;
    }

    // ETM extraction should stop at the previously discovered interface
    // boundary: first sequential endpoints or top-level output ports.
    if ((the_vertex != _current_port_vertex) &&
        (the_vertex != _current_logic_endpoint) &&
        _interface_logic_endpoints.contains(the_vertex)) {
      if (_state == kPropagateSlew) {
        the_vertex->set_is_slew_prop();
      } else if (_state == kPropagateDelay) {
        the_vertex->set_is_delay_prop();
      } else if (_state == kPropagateATFromPort) {
        the_vertex->set_is_fwd();
      }
      return 1;
    }

    // Clock-to-output ETM extraction should launch from the discovered clock
    // pin itself. Walking further upstream into the clock tree mixes extra
    // clock-slew families into the local clk->q arc buckets, which then
    // contaminates the boundary delay chosen for the output arc.
    if (the_vertex->is_clock() && !characterize_clock_path) {
      if (_state == kPropagateSlew) {
        the_vertex->set_is_slew_prop();
      } else if (_state == kPropagateDelay) {
        the_vertex->set_is_delay_prop();
      } else if (_state == kPropagateATFromPort) {
        the_vertex->set_is_fwd();
      }
      return 1;
    }

    // bwd
    FOREACH_SNK_ARC(the_vertex, snk_arc) {
      if (!snk_arc->isDelayArc()) {
        continue;
      }

      (*this)(snk_arc->get_src());
      // compute the slew and delay along the arc.
      (*this)(snk_arc);
    }

    if (_state == kPropagateSlew) {
      the_vertex->set_is_slew_prop();
    } else if (_state == kPropagateDelay) {
      the_vertex->set_is_delay_prop();
    } else if (_state == kPropagateATFromPort) {
      the_vertex->set_is_fwd();
    }
  }

  return 1;
}

/**
 * @brief propagate from the arc.
 *
 * @param the_arc
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaArc* the_arc) {
  if (_state == kPropagateSlew) {
    // propagate the slew along the arc.
    StaSlewPropagation slew_propagation;
    slew_propagation.set_data_epoch_filter(_characterization_epoch);
    // output port need propagate slew too, for construct transition table.
    slew_propagation.set_propagate_output_port();
    slew_propagation(the_arc);
  } else if (_state == kPropagateDelay) {
    // propagate the delay along the arc.
    StaDelayPropagation delay_propagation;
    delay_propagation.set_data_epoch_filter(_characterization_epoch);
    delay_propagation(the_arc);
  } else if (_state == kPropagateATFromPort) {
    // propagate the AT along the arc.
    StaFwdPropagation fwd_propagation;
    fwd_propagation.set_data_epoch_filter(_characterization_epoch);
    fwd_propagation(the_arc);
  } else if (_state == kBackPropagateRTToPort) {
    // propagate the RT along the arc.
    StaBwdPropagation bwd_propagation;
    bwd_propagation(the_arc);
  }

  return 1;
}

/**
 * @brief character timing is to extract the timing model of the design.
 * traverse from the port vertex to the first sequential cell. When reach to
 * the endpoint, build the constrain lib arc of the timing model.
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaGraph* the_graph) {
  // init the data and propgated way.
  init(the_graph);

  // first collect the interface logic endpoint.
  collectInterfaceLogicEndPoint(the_graph);

  // then check and break loop.
  checkAndBreakLoop(the_graph);

  // then propagate the slew from the port to the first sequential cell.
  propagateSlew(the_graph);

  // then propagate the delay from the port to the first sequential cell.
  propagateDelay(the_graph);

  // then propagate the AT from port to the first sequential cell.
  propagateATFromPort(the_graph);

  // then back propagate the RT from endpoint to port. not need propagated RT
  // now, temporary mask code below. backPropagateRTToPort(the_graph);

  // finaly gen the timing model.
  genTimingModel(the_graph, _model_path.c_str());

  return 1;
}

void StaCharacterTiming::snapshotFullStaClockPinSlew(StaGraph* the_graph) {
  _preserved_clock_pin_slew_ns.clear();
  _preserved_full_sta_pin_slew_ns.clear();

  auto snapshot_vertex = [this](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    size_t snapshot_entries = 0;
    const bool snapshot_eligible =
        the_vertex->is_clock() || the_vertex->is_sdc_clock_pin();
    for (auto analysis_mode : {AnalysisMode::kMax, AnalysisMode::kMin}) {
      for (auto trans_type : {TransType::kRise, TransType::kFall}) {
        auto* slew_data = the_vertex->getWorstSlewData(analysis_mode, trans_type);
        if (!slew_data) {
          continue;
        }

        const double slew_ns = FS_TO_NS(slew_data->get_slew());
        _preserved_full_sta_pin_slew_ns[std::make_tuple(the_vertex, analysis_mode,
                                                        trans_type)] = slew_ns;

        if (snapshot_eligible && analysis_mode == _analysis_mode) {
          _preserved_clock_pin_slew_ns[{the_vertex, trans_type}] = slew_ns;
          ++snapshot_entries;
        }
      }
    }

    traceClockSlewVertexSnapshot(the_vertex, snapshot_eligible,
                                 snapshot_entries);
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    snapshot_vertex(the_vertex);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    snapshot_vertex(assistant_vertex.get());
  }
}

void StaCharacterTiming::snapshotFullStaSeqCheckData(StaGraph* the_graph) {
  _preserved_seq_check_data.clear();

  auto* ista = Sta::getOrCreateSta();
  if (!ista) {
    return;
  }

  auto snapshot_endpoint = [this, ista](StaVertex* endpoint_vertex,
                                        AnalysisMode analysis_mode) {
    if (!endpoint_vertex) {
      return;
    }

    auto seq_data_vec = ista->getSeqData(endpoint_vertex, analysis_mode);
    if (seq_data_vec.empty()) {
      return;
    }

    auto& endpoint_snapshots = _preserved_seq_check_data[endpoint_vertex];
    for (auto* seq_data : seq_data_vec) {
      auto* seq_delay_data = seq_data ? seq_data->get_delay_data() : nullptr;
      auto* capture_clock_data =
          seq_data ? seq_data->get_capture_clock_data() : nullptr;
      if (!seq_data || !seq_delay_data || !capture_clock_data ||
          !seq_data->get_check_arc()) {
        continue;
      }

      auto data_terms = extractBoundaryDelayTerms(seq_data->getPathDelayData());
      auto capture_clock_terms =
          extractBoundaryDelayTerms(capture_clock_data->getPathData());
      if (!data_terms || !capture_clock_terms) {
        continue;
      }

      PreservedSeqCheckSnapshot snapshot;
      snapshot.check_arc = seq_data->get_check_arc();
      snapshot.capture_clock_vertex = capture_clock_data->get_own_vertex();
      snapshot.capture_clock_start_vertex = capture_clock_terms->start_vertex;
      snapshot.data_start_vertex = data_terms->start_vertex;
      snapshot.capture_analysis_mode = capture_clock_data->get_delay_type();
      snapshot.data_start_trans_type = data_terms->start_trans_type;
      snapshot.clock_trans_type = capture_clock_data->get_trans_type();
      snapshot.data_start_arrive_fs = data_terms->start_arrive_fs;
      snapshot.data_end_arrive_fs = data_terms->end_arrive_fs;
      snapshot.seq_arrive_time_fs = seq_data->getArriveTime();
      snapshot.required_time_fs = seq_data->getRequireTime();
      snapshot.capture_clock_start_arrive_fs =
          capture_clock_terms->start_arrive_fs;
      snapshot.capture_clock_end_arrive_fs =
          capture_clock_terms->end_arrive_fs;
      snapshot.capture_edge_fs = seq_data->getCaptureEdge();
      snapshot.constrain_value_fs = seq_data->get_constrain_value();
      snapshot.uncertainty_fs = seq_data->get_uncertainty().value_or(0);
      snapshot.cppr_fs = seq_data->get_cppr().value_or(0);

      const bool trace_seq_snapshot =
          shouldTraceCharacterizationPin(endpoint_vertex->getName().c_str()) ||
          (data_terms->start_vertex &&
           shouldTraceCharacterizationPin(data_terms->start_vertex->getName().c_str()));
      if (trace_seq_snapshot) {
        LOG_INFO << "[character_timing][seq-snapshot] endpoint="
                 << endpoint_vertex->getName() << " analysis_mode="
                 << analysisModeName(analysis_mode) << " check_arc="
                 << (seq_data->get_check_arc()
                         ? seq_data->get_check_arc()->get_src()->getName() + "->" +
                               seq_data->get_check_arc()->get_snk()->getName()
                         : "null")
                 << " seq_delay_trans="
                 << transTypeName(seq_delay_data->get_trans_type())
                 << " data_start_vertex="
                 << (data_terms->start_vertex ? data_terms->start_vertex->getName()
                                              : "null")
                 << " data_start_trans="
                 << transTypeName(data_terms->start_trans_type)
                 << " data_start_arrive_ns="
                 << FS_TO_NS(data_terms->start_arrive_fs)
                 << " data_end_arrive_ns=" << FS_TO_NS(data_terms->end_arrive_fs)
                 << " data_delay_ns=" << FS_TO_NS(data_terms->delay_fs)
                 << " seq_arrive_ns=" << FS_TO_NS(snapshot.seq_arrive_time_fs)
                 << " launch_clock_vertex="
                 << (seq_data->get_launch_clock_data()
                         ? seq_data->get_launch_clock_data()->get_own_vertex()->getName()
                         : "null")
                 << " capture_clock_vertex="
                 << (capture_clock_data ? capture_clock_data->get_own_vertex()->getName()
                                        : "null")
                 << " capture_clk_start_vertex="
                 << (capture_clock_terms->start_vertex
                         ? capture_clock_terms->start_vertex->getName()
                         : "null")
                 << " capture_clk_start_arrive_ns="
                 << FS_TO_NS(capture_clock_terms->start_arrive_fs)
                 << " capture_clk_end_arrive_ns="
                 << FS_TO_NS(capture_clock_terms->end_arrive_fs)
                 << " capture_clk_delay_ns="
                 << FS_TO_NS(capture_clock_terms->delay_fs)
                 << " capture_edge_ns=" << FS_TO_NS(snapshot.capture_edge_fs)
                 << " require_ns=" << FS_TO_NS(seq_data->getRequireTime())
                 << " constrain_ns=" << FS_TO_NS(seq_data->get_constrain_value())
                 << " uncertainty_ns=" << FS_TO_NS(snapshot.uncertainty_fs)
                 << " cppr_ns=" << FS_TO_NS(snapshot.cppr_fs);
      }

      endpoint_snapshots.push_back(snapshot);
    }
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    snapshot_endpoint(the_vertex, AnalysisMode::kMax);
    snapshot_endpoint(the_vertex, AnalysisMode::kMin);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    snapshot_endpoint(assistant_vertex.get(), AnalysisMode::kMax);
    snapshot_endpoint(assistant_vertex.get(), AnalysisMode::kMin);
  }
}

/**
 * @brief init the data propagated way, which may be graph based or path
 * based.Then set the init slew data.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::init(StaGraph* the_graph) {
  auto init_clock_path_delay_data = [this](StaVertex* the_vertex) {
    StaData* clock_data = nullptr;
    FOREACH_CLOCK_DATA(the_vertex, clock_data) {
      auto* launch_clock_data = dynamic_cast<StaClockData*>(clock_data);
      if (!launch_clock_data) {
        continue;
      }

      auto* path_delay_data = new StaPathDelayData(
          launch_clock_data->get_delay_type(),
          launch_clock_data->get_trans_type(), 0, launch_clock_data,
          the_vertex);
      path_delay_data->set_data_epoch(_characterization_epoch);
      path_delay_data->set_launch_delay_data(path_delay_data);
      the_vertex->addData(path_delay_data);
    }
  };

  auto seed_preserved_clock_slew = [this](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    StaData* slew_data = nullptr;
    FOREACH_SLEW_DATA(the_vertex, slew_data) {
      auto* vertex_slew_data = dynamic_cast<StaSlewData*>(slew_data);
      if (!vertex_slew_data) {
        continue;
      }

      auto snapshot_iter = _preserved_clock_pin_slew_ns.find(
          {the_vertex, vertex_slew_data->get_trans_type()});
      if (snapshot_iter == _preserved_clock_pin_slew_ns.end() ||
          snapshot_iter->second <= 0.0) {
        continue;
      }

      vertex_slew_data->set_slew(NS_TO_FS(snapshot_iter->second));
    }
  };

  snapshotFullStaClockPinSlew(the_graph);
  snapshotFullStaSeqCheckData(the_graph);
  resetCharacterizationPayload(the_graph);

  StaVertex* the_vertex;
  FOREACH_VERTEX(the_graph, the_vertex) {
    // the_vertex->setPathBasedPropagated(); // not use path base for run time.

    if (the_vertex->is_port()) {
      // set init slew or AT.
      auto* the_port = the_vertex->get_design_obj();
      if (the_port->isInput()) {
        the_vertex->initSlewData(
            0, true, true,
            _characterization_epoch);  // TODO(to taosimin) need decide the
        // discrete init slew data point.
        the_vertex->initPathDelayData(0, true, _characterization_epoch);
      }
    } else if (the_vertex->is_clock()) {
      // Clock-to-output characterization needs fresh epoch-local launch data,
      // but it must preserve the full-STA clock-pin transition that drove the
      // original arc delay.
      the_vertex->initSlewData(0, true, true, _characterization_epoch);
      seed_preserved_clock_slew(the_vertex);
      init_clock_path_delay_data(the_vertex);
    }
  }

  return 1;
}

/**
 * @brief collect the interface logic endpoint, so then we can propagate slew
 * delay AT from the endpoint.
 *
 * @return unsigned
 */
unsigned StaCharacterTiming::collectInterfaceLogicEndPoint(
    StaGraph* the_graph) {
  // capture the interface logic endpoint.
  // traverse from the port vertex to the first sequential cell, collect the
  // endpoints.
  _state = kCollectEndpoints;
  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();
    if (the_port->isInput()) {
      _current_port_vertex = port_vertex;
      (*this)(port_vertex);
    } else {
      _interface_logic_endpoints.insert(port_vertex);
    }
  }

  return 1;
}

/**
 * @brief check and break loop before propagated.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::checkAndBreakLoop(StaGraph* the_graph) {
  StaGraph logic_graph(the_graph->get_nl());

  for (auto* the_end_point : _interface_logic_endpoints) {
    logic_graph.addEndVertex(the_end_point);
  }

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    if (port_vertex->is_start()) {
      logic_graph.addStartVertex(port_vertex);
    }
  }

  StaCombLoopCheck comb_loop_check;
  comb_loop_check(&logic_graph);

  return 1;
}

/**
 * @brief propagate slew from the port to the first sequential cell
 * endpoint.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateSlew(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate slew start.";
  _state = kPropagateSlew;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate slew end.";

  return 1;
}

/**
 * @brief propagate delay from the port to the first sequential cell
 * endpoint.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateDelay(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate delay start.";
  _state = kPropagateDelay;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate delay end.";

  return 1;
}

/**
 * @brief propagate AT from port to the first sequential cell..
 *
 * @param port_vertex
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateATFromPort(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate AT start.";
  _state = kPropagateATFromPort;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate AT end.";
  return 1;
}

/**
 * @brief propagate RT from endpoint to port, if reach to the clock pin, need
 * mark the propagated port as clock port.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::backPropagateRTToPort(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate RT start.";

  {
    _state = kBackPropagateRTToPort;

    unsigned num_threads = getNumThreads();
    ThreadPool pool(num_threads);

    StaVertex* port_vertex;
    FOREACH_PORT_VERTEX(the_graph, port_vertex) {
      if (port_vertex->is_start()) {
#if 0
      (*this)(port_vertex);
#else
        // enqueue and store future
        pool.enqueue([this](StaVertex* port_vertex) { (*this)(port_vertex); },
                     port_vertex);

#endif
      }
    }
  }

  LOG_INFO << "character timing propagate RT end.";
  return 1;
}

/**
 * @brief generate the timing model as lib format.
 *
 * @param model_path
 * @return unsigned
 */
unsigned StaCharacterTiming::genTimingModel(StaGraph* the_graph,
                                            const char* model_path) {
  LOG_INFO << "gen timing model start.";
  _state = kGenTimingModel;

  _design_timing_model =
      std::make_unique<LibLibrary>(the_graph->get_nl()->get_name());
  if (auto* ista = Sta::getOrCreateSta(); ista) {
    auto* reference_lib = [&]() -> LibLibrary* {
      std::map<LibLibrary*, std::pair<int, int>> lib_usage;
      Instance* inst = nullptr;
      FOREACH_INSTANCE(the_graph->get_nl(), inst) {
        auto* inst_cell = inst->get_inst_cell();
        auto* owner_lib = inst_cell ? inst_cell->get_owner_lib() : nullptr;
        if (!owner_lib) {
          continue;
        }

        auto& [total_count, stdcell_count] = lib_usage[owner_lib];
        ++total_count;
        if (!inst_cell->isMacroCell()) {
          ++stdcell_count;
        }
      }

      LibLibrary* selected_lib = nullptr;
      auto selected_key = std::make_tuple(-1, -1, std::string());
      for (const auto& [lib, usage] : lib_usage) {
        auto current_key =
            std::make_tuple(usage.second, usage.first, lib->get_lib_name());
        if (!selected_lib || current_key > selected_key) {
          selected_lib = lib;
          selected_key = current_key;
        }
      }

      if (selected_lib) {
        return selected_lib;
      }

      auto& source_libs = ista->getAllLib();
      return source_libs.empty() ? nullptr : source_libs.front().get();
    }();

    if (reference_lib) {
      if (const auto& comment = reference_lib->get_comment(); comment) {
        _design_timing_model->set_comment(*comment);
      }
      if (const auto& simulation = reference_lib->get_simulation();
          simulation) {
        _design_timing_model->set_simulation(*simulation);
      }
      for (const auto& feature : reference_lib->get_library_features()) {
        _design_timing_model->add_library_feature(feature);
      }
      if (const auto& leakage_power_unit =
              reference_lib->get_leakage_power_unit();
          leakage_power_unit) {
        _design_timing_model->set_leakage_power_unit(*leakage_power_unit);
      }
      if (const auto& current_unit = reference_lib->get_current_unit_name();
          current_unit) {
        _design_timing_model->set_current_unit_name(*current_unit);
      }
      if (const auto& voltage_unit = reference_lib->get_voltage_unit_name();
          voltage_unit) {
        _design_timing_model->set_voltage_unit_name(*voltage_unit);
      }
      _design_timing_model->set_cap_unit(reference_lib->get_cap_unit());
      _design_timing_model->set_resistance_unit(
          reference_lib->get_resistance_unit());
      _design_timing_model->set_time_unit(reference_lib->get_time_unit());
      if (const auto& nom_process = reference_lib->get_nom_process();
          nom_process) {
        _design_timing_model->set_nom_process(*nom_process);
      }
      _design_timing_model->set_nom_voltage(reference_lib->get_nom_voltage());
      if (const auto& nom_temperature = reference_lib->get_nom_temperature();
          nom_temperature) {
        _design_timing_model->set_nom_temperature(*nom_temperature);
      }
      _design_timing_model->set_slew_lower_threshold_pct_rise(
          reference_lib->get_slew_lower_threshold_pct_rise() * 100.0);
      _design_timing_model->set_slew_upper_threshold_pct_rise(
          reference_lib->get_slew_upper_threshold_pct_rise() * 100.0);
      _design_timing_model->set_slew_lower_threshold_pct_fall(
          reference_lib->get_slew_lower_threshold_pct_fall() * 100.0);
      _design_timing_model->set_slew_upper_threshold_pct_fall(
          reference_lib->get_slew_upper_threshold_pct_fall() * 100.0);
      _design_timing_model->set_input_threshold_pct_rise(
          reference_lib->get_input_threshold_pct_rise() * 100.0);
      _design_timing_model->set_input_threshold_pct_fall(
          reference_lib->get_input_threshold_pct_fall() * 100.0);
      _design_timing_model->set_output_threshold_pct_rise(
          reference_lib->get_output_threshold_pct_rise() * 100.0);
      _design_timing_model->set_output_threshold_pct_fall(
          reference_lib->get_output_threshold_pct_fall() * 100.0);
      _design_timing_model->set_slew_derate_from_library(
          reference_lib->get_slew_derate_from_library());

      if (const auto& default_max_transition =
              reference_lib->get_default_max_transition();
          default_max_transition) {
        _design_timing_model->set_default_max_transition(
            *default_max_transition);
      }
      if (const auto& default_max_fanout =
              reference_lib->get_default_max_fanout();
          default_max_fanout) {
        _design_timing_model->set_default_max_fanout(*default_max_fanout);
      }
      if (const auto& default_fanout_load =
              reference_lib->get_default_fanout_load();
          default_fanout_load) {
        _design_timing_model->set_default_fanout_load(*default_fanout_load);
      }
    }
  }
  auto design_timing_cell = std::make_unique<LibCell>(
      the_graph->get_nl()->get_name(), _design_timing_model.get());
  design_timing_cell->set_is_macro();

  double design_cell_area = 0.0;
  Instance* inst = nullptr;
  FOREACH_INSTANCE(the_graph->get_nl(), inst) {
    if (auto* inst_cell = inst->get_inst_cell(); inst_cell) {
      design_cell_area += inst_cell->get_cell_area();
    }
  }
  design_timing_cell->set_cell_area(design_cell_area);

  auto template_variable_name = [](LibLutTableTemplate::Variable variable) {
    switch (variable) {
      case LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
        return "total_output_net_capacitance";
      case LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
        return "input_net_transition";
      case LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
        return "constrained_pin_transition";
      case LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
        return "related_pin_transition";
      case LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
        return "input_transition_time";
      case LibLutTableTemplate::Variable::TIME:
        return "time";
      case LibLutTableTemplate::Variable::INPUT_VOLTAGE:
        return "input_voltage";
      case LibLutTableTemplate::Variable::OUTPUT_VOLTAGE:
        return "output_voltage";
      case LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT:
        return "input_noise_height";
      case LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH:
        return "input_noise_width";
      case LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE:
        return "normalized_voltage";
    }
    return "total_output_net_capacitance";
  };

  struct LoadAxisInfo {
    LibLutTableTemplate::Variable variable;
    std::vector<double> values;
  };

  std::map<std::string, LibLutTableTemplate*> load_template_cache;
  auto clone_axis = [](const std::vector<double>& axis_values) {
    auto axis = std::make_unique<LibAxis>("index");
    std::vector<std::unique_ptr<LibAttrValue>> values;
    values.reserve(axis_values.size());
    for (double axis_value : axis_values) {
      values.emplace_back(std::make_unique<LibFloatValue>(axis_value));
    }
    axis->set_axis_values(std::move(values));
    return axis;
  };

  auto get_or_create_load_template =
      [this, &clone_axis, &load_template_cache, &template_variable_name](
          const LoadAxisInfo& load_axis_info) -> LibLutTableTemplate* {
    std::string cache_key =
        std::to_string(static_cast<int>(load_axis_info.variable));
    for (double axis_value : load_axis_info.values) {
      cache_key.append("|");
      cache_key.append(std::to_string(axis_value));
    }

    if (auto iter = load_template_cache.find(cache_key);
        iter != load_template_cache.end()) {
      return iter->second;
    }

    std::string template_name =
        "template_" + std::to_string(load_template_cache.size() + 1);
    auto lut_template =
        std::make_unique<LibLutTableTemplate>(template_name.c_str());
    lut_template->set_template_variable1(
        template_variable_name(load_axis_info.variable));
    lut_template->addAxis(clone_axis(load_axis_info.values));
    auto* lut_template_ptr = lut_template.get();
    _design_timing_model->addLutTemplate(std::move(lut_template));
    load_template_cache.emplace(std::move(cache_key), lut_template_ptr);
    return lut_template_ptr;
  };

  auto extract_load_axis = [](LibTable* reference_table)
      -> std::optional<LoadAxisInfo> {
    if (!reference_table) {
      return std::nullopt;
    }

    auto* table_template = reference_table->get_table_template();
    if (!table_template) {
      return std::nullopt;
    }

    auto& axes = reference_table->get_axes();
    if (axes.empty()) {
      return std::nullopt;
    }

    auto build_axis_info = [&axes](int axis_index,
                                   LibLutTableTemplate::Variable variable)
        -> std::optional<LoadAxisInfo> {
      if (axis_index >= axes.size()) {
        return std::nullopt;
      }

      std::vector<double> axis_values;
      auto& raw_axis_values = axes[axis_index]->get_axis_values();
      axis_values.reserve(raw_axis_values.size());
      for (const auto& axis_value : raw_axis_values) {
        axis_values.emplace_back(
            dynamic_cast<LibFloatValue*>(axis_value.get())->getFloatValue());
      }

      if (axis_values.empty()) {
        return std::nullopt;
      }

      return LoadAxisInfo{variable, std::move(axis_values)};
    };

    if (auto variable1 = table_template->get_template_variable1();
        variable1
        && *variable1
               == LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE) {
      return build_axis_info(0, *variable1);
    }

    if (auto variable2 = table_template->get_template_variable2();
        variable2
        && *variable2
               == LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE) {
      return build_axis_info(1, *variable2);
    }

    return std::nullopt;
  };

  auto build_scalar_table = [](LibTable::TableType table_type, double table_value) {
    auto table = std::make_unique<LibTable>(table_type, nullptr);
    table->addTableValue(std::make_unique<LibFloatValue>(table_value));
    return table;
  };

  auto convert_lib_cap_to_internal_pf = [this](double raw_cap) {
    return ConvertCapUnit(_design_timing_model->get_cap_unit(),
                          CapacitiveUnit::kPF, raw_cap);
  };

  auto resolve_export_port_type = [](DesignObject* design_port) {
    if (auto* sta_port = dynamic_cast<Port*>(design_port); sta_port) {
      auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
      auto* db_adapter = timing_engine
                             ? dynamic_cast<TimingIDBAdapter*>(
                                   timing_engine->get_db_adapter())
                             : nullptr;
      auto* db_port = db_adapter ? db_adapter->staToDb(sta_port) : nullptr;
      auto* db_term = db_port ? db_port->get_term() : nullptr;
      if (db_term && (db_term->is_power() || db_term->is_ground())) {
        return LibPort::LibertyPortType::kInput;
      }
    }

    if (design_port->isInput()) {
      return LibPort::LibertyPortType::kInput;
    }
    if (design_port->isOutput()) {
      return LibPort::LibertyPortType::kOutput;
    }
    if (design_port->isInout()) {
      return LibPort::LibertyPortType::kInOut;
    }
    return LibPort::LibertyPortType::kDefault;
  };

  // construct the data port to clock port check arc.
  auto construct_port_check_arc = [&design_timing_cell, this](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    if (!_port_to_logic_endpoint.contains(port_vertex)) {
      return;
    }

    struct CheckArcCandidate {
      std::string related_pin;
      std::string timing_type;
      double rise_constraint = 0.0;
      double fall_constraint = 0.0;
      bool has_rise_constraint = false;
      bool has_fall_constraint = false;
      bool is_valid = false;
    };

    std::map<std::pair<std::string, std::string>, CheckArcCandidate>
        aggregated_candidates;
    auto logic_endpoints = _port_to_logic_endpoint.values(port_vertex);
    const bool trace_port =
        shouldTraceCharacterizationPin(port_vertex->getName().c_str());

    auto collect_check_arc_candidate = [&](auto* logic_endpoint,
                                           AnalysisMode constraint_mode,
                                           const char* rising_timing_type,
                                           const char* falling_timing_type) {
      for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
        auto local_data_candidates = collectBoundaryDelayCandidatesForEpoch(
            logic_endpoint, constraint_mode, _characterization_epoch,
            port_vertex, input_trans_type);
        if (local_data_candidates.empty()) {
          continue;
        }

        std::vector<StaArc*> endpoint_check_arcs;
        for (auto* endpoint_arc : logic_endpoint->get_snk_arcs()) {
          const bool mode_matches =
              (constraint_mode == AnalysisMode::kMax &&
               (endpoint_arc->isSetupArc() || endpoint_arc->isRecoveryArc())) ||
              (constraint_mode == AnalysisMode::kMin &&
               (endpoint_arc->isHoldArc() || endpoint_arc->isRemovalArc()));
          if (mode_matches) {
            endpoint_check_arcs.push_back(endpoint_arc);
          }
        }
        if (endpoint_check_arcs.empty()) {
          continue;
        }

        for (auto* endpoint_check_arc : endpoint_check_arcs) {
          auto* clk_point = endpoint_check_arc->get_src();
          auto clock_port_vertexes = _logic_clkpoint_to_port.values(clk_point);
          if (clock_port_vertexes.empty()) {
            LOG_INFO << clk_point->getName() << " clock port vertex is empty.";
            continue;
          }

          auto* clock_port_vertex = clock_port_vertexes.front();
          std::string timing_type = endpoint_check_arc->isRisingEdgeCheck()
                                        ? rising_timing_type
                                        : falling_timing_type;
          auto candidate_key =
              std::make_pair(clock_port_vertex->getName(), timing_type);
          auto& candidate = aggregated_candidates[candidate_key];
          if (!candidate.is_valid) {
            candidate.related_pin = candidate_key.first;
            candidate.timing_type = candidate_key.second;
            candidate.is_valid = true;
          }

          auto clock_trans_type = endpoint_check_arc->isRisingEdgeCheck()
                                      ? TransType::kRise
                                      : TransType::kFall;
          auto capture_analysis_mode =
              constraint_mode == AnalysisMode::kMax ? AnalysisMode::kMin
                                                    : AnalysisMode::kMax;
          auto local_clock_candidates = collectBoundaryDelayCandidatesForEpoch(
              clk_point, capture_analysis_mode, _characterization_epoch,
              clock_port_vertex, std::nullopt, clock_trans_type);
          if (local_clock_candidates.empty()) {
            if (trace_port) {
              LOG_INFO << "[character_timing][check-clock-miss] port="
                       << port_vertex->getName() << " endpoint="
                       << logic_endpoint->getName() << " mode="
                       << analysisModeName(constraint_mode)
                       << " input_trans=" << transTypeName(input_trans_type)
                       << " timing_type=" << timing_type << " clock_port="
                       << clock_port_vertex->getName() << " clk_point="
                       << clk_point->getName() << " clock_trans="
                       << transTypeName(clock_trans_type);
            }
            continue;
          }

          auto* inst_check_arc = dynamic_cast<StaInstArc*>(endpoint_check_arc);
          auto* reference_check_arc =
              inst_check_arc ? inst_check_arc->get_lib_arc() : nullptr;
          auto accumulate_constraint = [&](double data_arrival_delta_ns,
                                           double clock_arrival_delta_ns,
                                           double check_margin_ns,
                                           double capture_edge_ns,
                                           const char* source_tag) {
                double constraint_ns =
                    constraint_mode == AnalysisMode::kMax
                        ? data_arrival_delta_ns - clock_arrival_delta_ns +
                              check_margin_ns
                        : clock_arrival_delta_ns - data_arrival_delta_ns +
                              check_margin_ns;

                if (trace_port) {
                  LOG_INFO << "[character_timing][check] port="
                           << port_vertex->getName() << " endpoint="
                           << logic_endpoint->getName() << " mode="
                           << (constraint_mode == AnalysisMode::kMax ? "max"
                                                                      : "min")
                           << " input_trans="
                           << (input_trans_type == TransType::kRise ? "rise"
                                                                    : "fall")
                           << " timing_type=" << timing_type
                           << " related_clk=" << clock_port_vertex->getName()
                           << " source=" << source_tag
                           << " data_arrival_delta_ns="
                           << data_arrival_delta_ns
                           << " clock_arrival_delta_ns="
                           << clock_arrival_delta_ns
                           << " capture_edge_ns=" << capture_edge_ns
                           << " check_margin_ns=" << check_margin_ns
                           << " formula=data-clock+margin"
                           << " constraint_ps=" << constraint_ns;
                }

                if (input_trans_type == TransType::kRise) {
                  candidate.rise_constraint =
                      candidate.has_rise_constraint
                          ? std::max(candidate.rise_constraint, constraint_ns)
                          : constraint_ns;
                  candidate.has_rise_constraint = true;
                } else if (input_trans_type == TransType::kFall) {
                  candidate.fall_constraint =
                      candidate.has_fall_constraint
                          ? std::max(candidate.fall_constraint, constraint_ns)
                          : constraint_ns;
                  candidate.has_fall_constraint = true;
                }

                return true;
              };

          auto clock_slew_ns = findVertexSlewNsForEpochWithStart(
              clk_point, capture_analysis_mode, clock_trans_type,
              _characterization_epoch, clock_port_vertex);
          if (!clock_slew_ns) {
            clock_slew_ns = findVertexSlewNsForEpoch(
                clk_point, capture_analysis_mode, clock_trans_type,
                _characterization_epoch);
          }
          if (!clock_slew_ns) {
            auto preserved_clock_iter =
                _preserved_clock_pin_slew_ns.find({clk_point, clock_trans_type});
            if (preserved_clock_iter != _preserved_clock_pin_slew_ns.end()) {
              clock_slew_ns = preserved_clock_iter->second;
            }
          }

          if (trace_port) {
            std::ostringstream local_data_arrival_deltas;
            local_data_arrival_deltas << "[";
            for (size_t i = 0; i < local_data_candidates.size(); ++i) {
              if (i != 0) {
                local_data_arrival_deltas << ",";
              }
              local_data_arrival_deltas
                  << FS_TO_NS(local_data_candidates[i].terms.delay_fs)
                                << ":" << transTypeName(
                                               local_data_candidates[i]
                                                   .terms.end_trans_type);
            }
            local_data_arrival_deltas << "]";

            std::ostringstream local_clock_arrival_deltas;
            local_clock_arrival_deltas << "[";
            for (size_t i = 0; i < local_clock_candidates.size(); ++i) {
              if (i != 0) {
                local_clock_arrival_deltas << ",";
              }
              auto clock_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_clock_candidates[i].delay_data, _characterization_epoch);
              local_clock_arrival_deltas
                  << FS_TO_NS(local_clock_candidates[i].terms.delay_fs)
                  << "(total)/"
                  << (clock_breakdown ? clock_breakdown->cell_ns : 0.0)
                  << "(cell)/"
                  << (clock_breakdown ? clock_breakdown->net_ns : 0.0)
                  << "(net):" << transTypeName(
                                     local_clock_candidates[i]
                                         .terms.start_trans_type);
            }
            local_clock_arrival_deltas << "]";

            std::ostringstream all_clock_candidates;
            all_clock_candidates << "[";
            auto all_clock_delay_candidates = collectBoundaryDelayCandidatesForEpoch(
                clk_point, capture_analysis_mode, _characterization_epoch,
                nullptr, std::nullopt, clock_trans_type);
            for (size_t i = 0; i < all_clock_delay_candidates.size(); ++i) {
              if (i != 0) {
                all_clock_candidates << ",";
              }
              const auto& clock_candidate = all_clock_delay_candidates[i];
              all_clock_candidates
                  << (clock_candidate.terms.start_vertex
                          ? clock_candidate.terms.start_vertex->getName()
                          : "null")
                  << "->"
                  << (clock_candidate.terms.end_vertex
                          ? clock_candidate.terms.end_vertex->getName()
                          : "null")
                  << ":start="
                  << FS_TO_NS(clock_candidate.terms.start_arrive_fs)
                  << ":end="
                  << FS_TO_NS(clock_candidate.terms.end_arrive_fs)
                  << ":delta="
                  << FS_TO_NS(clock_candidate.terms.delay_fs);
            }
            all_clock_candidates << "]";

            LOG_INFO << "[character_timing][check-local-candidates] port="
                     << port_vertex->getName() << " endpoint="
                     << logic_endpoint->getName() << " mode="
                     << analysisModeName(constraint_mode) << " input_trans="
                     << transTypeName(input_trans_type)
                     << " timing_type=" << timing_type
                     << " data_arrival_deltas_ns="
                     << local_data_arrival_deltas.str()
                     << " clock_arrival_deltas_ns="
                     << local_clock_arrival_deltas.str()
                     << " all_clock_candidates_ns="
                     << all_clock_candidates.str()
                     << " clock_slew_ns="
                     << (clock_slew_ns ? *clock_slew_ns : 0.0)
                     << " margin_source="
                     << (reference_check_arc ? "lib_arc" : "missing_lib_arc");
          }

          for (const auto& local_data_candidate : local_data_candidates) {
            std::vector<StaSeqPathData*> matched_seq_data;
            if (auto* ista = Sta::getOrCreateSta(); ista) {
              matched_seq_data =
                  ista->getSeqData(logic_endpoint, local_data_candidate.delay_data);
            }
            auto data_slew_ns = findVertexSlewNsForEpochWithStart(
                logic_endpoint, constraint_mode,
                local_data_candidate.terms.end_trans_type,
                _characterization_epoch,
                port_vertex, input_trans_type);
            if (!data_slew_ns) {
              data_slew_ns = findVertexSlewNsForEpoch(
                  logic_endpoint, constraint_mode,
                  local_data_candidate.terms.end_trans_type,
                  _characterization_epoch);
            }

            double check_margin_ns = 0.0;
            double check_margin_with_local_clock_slew_ns = 0.0;
            const double full_sta_check_margin_ns = FS_TO_NS(
                endpoint_check_arc->get_arc_delay(
                    constraint_mode, local_data_candidate.terms.end_trans_type));
            const auto preserved_full_sta_clock_slew =
                _preserved_full_sta_pin_slew_ns.find(std::make_tuple(
                    clk_point, capture_analysis_mode, clock_trans_type));
            const double check_clock_slew_ns =
                preserved_full_sta_clock_slew != _preserved_full_sta_pin_slew_ns.end()
                    ? preserved_full_sta_clock_slew->second
                    : clock_slew_ns.value_or(0.0);
            const double check_data_slew_ns = data_slew_ns.value_or(0.0);
            const double check_data_slew_axis =
                convertNsToReferenceTimeAxisUnit(reference_check_arc,
                                                check_data_slew_ns);
            const double local_data_slew_axis =
                convertNsToReferenceTimeAxisUnit(reference_check_arc,
                                                data_slew_ns.value_or(0.0));
            if (full_sta_check_margin_ns != 0.0) {
              check_margin_ns = full_sta_check_margin_ns;
              check_margin_with_local_clock_slew_ns = full_sta_check_margin_ns;
            } else if (reference_check_arc) {
              check_margin_ns = reference_check_arc->getDelayOrConstrainCheckNs(
                  local_data_candidate.terms.end_trans_type,
                  check_clock_slew_ns, check_data_slew_axis);
              check_margin_with_local_clock_slew_ns =
                  reference_check_arc->getDelayOrConstrainCheckNs(
                      local_data_candidate.terms.end_trans_type,
                      clock_slew_ns.value_or(0.0), local_data_slew_axis);
            }

            for (const auto& local_clock_candidate : local_clock_candidates) {
              auto data_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_data_candidate.delay_data, _characterization_epoch);
              auto clock_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_clock_candidate.delay_data, _characterization_epoch);
              auto clock_rebuilt_total_ns = rebuildPathDelayNsForEpoch(
                  local_clock_candidate.delay_data, _characterization_epoch);
              auto* clock_launch_clock_data =
                  local_clock_candidate.delay_data
                      ? local_clock_candidate.delay_data->get_launch_clock_data()
                      : nullptr;
              const double data_arrival_delta_ns =
                  FS_TO_NS(local_data_candidate.terms.delay_fs);
              // Current full-STA snapshots do not expose a trustworthy
              // propagated target-clock arrival delta for ideal-clock
              // input-to-register checks, while the epoch-local clock
              // candidates can explode into multi-cycle noise. Collapse the
              // subtraction to the matched data arrival term and let the
              // original sequential check arc provide the exported margin.
              const double clock_arrival_delta_ns = data_arrival_delta_ns;
              const double local_clock_arrival_delta_ns =
                  FS_TO_NS(local_clock_candidate.terms.delay_fs);
              auto data_rebuilt_total_ns = rebuildPathDelayNsForEpoch(
                  local_data_candidate.delay_data, _characterization_epoch);
              auto* data_launch_clock_data =
                  local_data_candidate.delay_data
                      ? local_data_candidate.delay_data->get_launch_clock_data()
                      : nullptr;
              if (trace_port) {
                std::ostringstream matched_seq_summary;
                matched_seq_summary << "[";
                for (size_t i = 0; i < matched_seq_data.size(); ++i) {
                  if (i != 0) {
                    matched_seq_summary << ",";
                  }
                  auto* seq_data = matched_seq_data[i];
                  auto* capture_clock_data =
                      seq_data ? seq_data->get_capture_clock_data() : nullptr;
                  auto capture_clock_terms =
                      capture_clock_data
                          ? extractBoundaryDelayTerms(
                                capture_clock_data->getPathData())
                          : std::nullopt;
                  matched_seq_summary
                      << "mode="
                      << (seq_data
                              ? analysisModeName(seq_data->getDelayType())
                              : "null")
                      << ":capture_start="
                      << (capture_clock_terms
                              ? FS_TO_NS(capture_clock_terms->start_arrive_fs)
                              : -1.0)
                      << ":capture_end="
                      << (capture_clock_terms
                              ? FS_TO_NS(capture_clock_terms->end_arrive_fs)
                              : -1.0)
                      << ":capture_delta="
                      << (capture_clock_terms
                              ? FS_TO_NS(capture_clock_terms->delay_fs)
                              : -1.0)
                      << ":require="
                      << (seq_data ? FS_TO_NS(seq_data->getRequireTime()) : -1.0)
                      << ":constrain="
                      << (seq_data ? FS_TO_NS(seq_data->get_constrain_value())
                                   : -1.0);
                }
                matched_seq_summary << "]";
                LOG_INFO << "[character_timing][check-local] port="
                         << port_vertex->getName() << " endpoint="
                         << logic_endpoint->getName() << " mode="
                         << analysisModeName(constraint_mode)
                         << " input_trans=" << transTypeName(input_trans_type)
                         << " timing_type=" << timing_type
                         << " data_end_trans="
                         << transTypeName(
                                local_data_candidate.terms.end_trans_type)
                         << " data_arrival_delta_ns="
                         << data_arrival_delta_ns
                         << " data_cell_delay_ns="
                         << (data_breakdown ? data_breakdown->cell_ns : -1.0)
                         << " data_net_delay_ns="
                         << (data_breakdown ? data_breakdown->net_ns : -1.0)
                         << " data_stored_arrival_ns="
                         << (local_data_candidate.delay_data
                                 ? FS_TO_NS(local_data_candidate.delay_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " data_rebuilt_total_ns="
                         << (data_rebuilt_total_ns ? *data_rebuilt_total_ns
                                                   : -1.0)
                         << " data_launch_clk_arrive_ns="
                         << (data_launch_clock_data
                                 ? FS_TO_NS(
                                       data_launch_clock_data->get_arrive_time())
                                 : -1.0)
                         << " local_clock_arrival_delta_ns="
                         << local_clock_arrival_delta_ns
                         << " clock_arrival_delta_ns="
                         << clock_arrival_delta_ns
                         << " clock_stored_arrival_ns="
                         << (local_clock_candidate.delay_data
                                 ? FS_TO_NS(local_clock_candidate.delay_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " clock_rebuilt_total_ns="
                         << (clock_rebuilt_total_ns ? *clock_rebuilt_total_ns
                                                    : -1.0)
                         << " clock_launch_clk_arrive_ns="
                         << (clock_launch_clock_data
                                 ? FS_TO_NS(clock_launch_clock_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " clock_net_delay_ns="
                         << (clock_breakdown ? clock_breakdown->net_ns : 0.0)
                         << " full_sta_clock_arrive_ns="
                         << ([&]() -> double {
                              auto full_clock_arrive_fs =
                                  clk_point->getClockArriveTime(
                                      capture_analysis_mode, clock_trans_type);
                              return full_clock_arrive_fs
                                         ? FS_TO_NS(*full_clock_arrive_fs)
                                         : -1.0;
                            })()
                         << " clock_start_trans="
                         << transTypeName(
                                local_clock_candidate.terms.start_trans_type)
                         << " check_clock_slew_ns="
                         << check_clock_slew_ns
                         << " local_clock_slew_ns="
                         << clock_slew_ns.value_or(0.0)
                         << " check_data_slew_ns="
                         << check_data_slew_ns
                         << " local_data_slew_ns="
                         << (data_slew_ns ? *data_slew_ns : 0.0)
                         << " check_margin_ns=" << check_margin_ns
                         << " check_margin_with_local_clock_slew_ns="
                         << check_margin_with_local_clock_slew_ns
                         << " matched_seq_data=" << matched_seq_summary.str();
              }

              accumulate_constraint(
                  data_arrival_delta_ns,
                  clock_arrival_delta_ns,
                  check_margin_ns, 0.0, "local_arrival_subtraction");
            }
          }
        }
      }
    };

    for (auto* logic_endpoint : logic_endpoints) {
      collect_check_arc_candidate(logic_endpoint, AnalysisMode::kMax,
                                  "setup_rising", "setup_falling");
      collect_check_arc_candidate(logic_endpoint, AnalysisMode::kMin,
                                  "hold_rising", "hold_falling");
    }

    for (const auto& [candidate_key, candidate] : aggregated_candidates) {
      if (!candidate.is_valid) {
        continue;
      }
      auto lib_arc = std::make_unique<LibArc>();
      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_src_port(candidate.related_pin.c_str());
      lib_arc->set_timing_type(candidate.timing_type.c_str());

      auto check_model = std::make_unique<LibCheckTableModel>();
      if (candidate.has_rise_constraint) {
        auto lib_rise_table = std::make_unique<LibTable>(
            LibTable::TableType::kRiseConstrain, nullptr);
        lib_rise_table->addTableValue(
            std::make_unique<LibFloatValue>(candidate.rise_constraint));
        check_model->addTable(std::move(lib_rise_table));
      }
      if (candidate.has_fall_constraint) {
        auto lib_fall_table = std::make_unique<LibTable>(
            LibTable::TableType::kFallConstrain, nullptr);
        lib_fall_table->addTableValue(
            std::make_unique<LibFloatValue>(candidate.fall_constraint));
        check_model->addTable(std::move(lib_fall_table));
      }
      if (!candidate.has_rise_constraint && !candidate.has_fall_constraint) {
        continue;
      }

      lib_arc->set_table_model(std::move(check_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    }
  };

  // construct the data input port to output port delay arc.
  auto construct_port_delay_arc = [&design_timing_cell, this, &build_scalar_table,
                                   &clone_axis, &extract_load_axis,
                                   &get_or_create_load_template](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    auto resolve_clock_related_pin_name =
        [](StaClockData* launch_clock_data) -> std::string {
      if (!launch_clock_data) {
        return {};
      }

      auto* launch_clock = launch_clock_data->get_prop_clock();
      if (!launch_clock) {
        return {};
      }

      auto* ista = Sta::getOrCreateSta();
      auto* constrain = ista ? ista->getConstrain() : nullptr;
      auto* sdc_clock = constrain
                            ? constrain->findClock(launch_clock->get_clock_name())
                            : nullptr;
      if (sdc_clock) {
        auto& clock_objs = sdc_clock->get_objs();
        auto port_iter = std::find_if(clock_objs.begin(), clock_objs.end(),
                                      [](DesignObject* obj) {
                                        return obj && obj->isPort();
                                      });
        if (port_iter != clock_objs.end()) {
          return (*port_iter)->get_name();
        }

        auto obj_iter = std::find_if(clock_objs.begin(), clock_objs.end(),
                                     [](DesignObject* obj) {
                                       return obj != nullptr;
                                     });
        if (obj_iter != clock_objs.end()) {
          return (*obj_iter)->get_name();
        }
      }

      return launch_clock->get_clock_name();
    };

    auto build_table_from_reference =
        [this, &build_scalar_table, &clone_axis, &extract_load_axis,
         &get_or_create_load_template, analysis_mode](
            StaPathDelayData* delay_data, StaSlewData* slew_data,
            double path_delay_ns, LibTable::TableType delay_table_type,
            LibTable::TableType slew_table_type,
            TransType block_offset_trans_type,
            TransType sampled_output_trans_type)
        -> std::pair<std::unique_ptr<LibTable>, std::unique_ptr<LibTable>> {
      double output_slew_ns =
          slew_data ? FS_TO_NS(slew_data->get_slew()) : 0.0;
      auto scalar_delay_table =
          build_scalar_table(delay_table_type, path_delay_ns);
      auto scalar_slew_table =
          build_scalar_table(slew_table_type, output_slew_ns);
      if (!delay_data) {
        LOG_INFO_FIRST_N(5)
            << "liberty alignment fallback: missing delay data.";
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      StaInstArc* final_inst_arc = nullptr;
      auto* driver_path_data =
          dynamic_cast<StaPathDelayData*>(delay_data->get_bwd());
      auto* driver_input_path_data = driver_path_data
                                         ? dynamic_cast<StaPathDelayData*>(
                                               driver_path_data->get_bwd())
                                         : nullptr;
      auto* driver_vertex =
          driver_path_data ? driver_path_data->get_own_vertex() : nullptr;
      auto* driver_input_vertex = driver_input_path_data
                                      ? driver_input_path_data->get_own_vertex()
                                      : nullptr;

      if (driver_vertex) {
        FOREACH_SNK_ARC(driver_vertex, snk_arc) {
          auto* inst_arc = dynamic_cast<StaInstArc*>(snk_arc);
          if (!inst_arc || !inst_arc->isDelayArc()) {
            continue;
          }

          if (!driver_input_vertex || snk_arc->get_src() == driver_input_vertex) {
            final_inst_arc = inst_arc;
            break;
          }
        }
      }

      if (!final_inst_arc) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: final inst arc not found for sink "
                             << delay_data->get_own_vertex()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto* reference_arc = final_inst_arc->get_lib_arc();
      auto* reference_delay_model =
          dynamic_cast<LibDelayTableModel*>(reference_arc->get_table_model());
      if (!reference_delay_model) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: reference delay model missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto* reference_delay_table =
          reference_delay_model->getTable(static_cast<int>(delay_table_type));
      auto* reference_slew_table =
          reference_delay_model->getTable(static_cast<int>(slew_table_type));
      auto load_axis_info = extract_load_axis(reference_delay_table);
      if (!load_axis_info) {
        load_axis_info = extract_load_axis(reference_slew_table);
      }
      if (!load_axis_info) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: load axis missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto pick_available_slew =
          [analysis_mode](StaVertex* vertex, TransType preferred_trans_type,
                          uint64_t data_epoch) -> std::optional<double> {
        if (!vertex) {
          return std::nullopt;
        }

        if (auto preferred_slew = findVertexSlewNsForEpoch(
                vertex, analysis_mode, preferred_trans_type, data_epoch)) {
          return preferred_slew;
        }

        if (auto opposite_slew = findVertexSlewNsForEpoch(
                vertex, analysis_mode, FLIP_TRANS(preferred_trans_type),
                data_epoch)) {
          return opposite_slew;
        }

        auto rise_slew = findVertexSlewNsForEpoch(vertex, analysis_mode,
                                                  TransType::kRise, data_epoch);
        auto fall_slew = findVertexSlewNsForEpoch(vertex, analysis_mode,
                                                  TransType::kFall, data_epoch);
        if (rise_slew && fall_slew) {
          if (analysis_mode == AnalysisMode::kMax) {
            return (*rise_slew > *fall_slew) ? rise_slew : fall_slew;
          }
          return (*rise_slew < *fall_slew) ? rise_slew : fall_slew;
        }

        return rise_slew ? rise_slew : fall_slew;
      };

      auto pick_preserved_clock_pin_slew =
          [this, analysis_mode](StaVertex* vertex,
                                TransType preferred_trans_type)
          -> std::optional<double> {
        if (!vertex) {
          return std::nullopt;
        }

        auto lookup = [this, vertex](TransType trans_type)
            -> std::optional<double> {
          auto snapshot_iter =
              _preserved_clock_pin_slew_ns.find({vertex, trans_type});
          return snapshot_iter == _preserved_clock_pin_slew_ns.end()
                     ? std::nullopt
                     : std::optional<double>(snapshot_iter->second);
        };

        if (auto preferred_slew = lookup(preferred_trans_type)) {
          return preferred_slew;
        }

        if (auto opposite_slew = lookup(FLIP_TRANS(preferred_trans_type))) {
          return opposite_slew;
        }

        auto rise_slew = lookup(TransType::kRise);
        auto fall_slew = lookup(TransType::kFall);
        if (rise_slew && fall_slew) {
          if (analysis_mode == AnalysisMode::kMax) {
            return (*rise_slew > *fall_slew) ? rise_slew : fall_slew;
          }
          return (*rise_slew < *fall_slew) ? rise_slew : fall_slew;
        }

        return rise_slew ? rise_slew : fall_slew;
      };

      auto pick_sampling_clock_slew =
          [this, &pick_preserved_clock_pin_slew](StaVertex* vertex,
                                                 TransType preferred_trans_type)
          -> std::optional<double> {
        if (!vertex || !vertex->is_clock()) {
          return std::nullopt;
        }

        auto direct_slew =
            pick_preserved_clock_pin_slew(vertex, preferred_trans_type);
        if (direct_slew && *direct_slew > 0.0) {
          return direct_slew;
        }

        auto mapped_ports = _logic_clkpoint_to_port.values(vertex);
        for (auto* port_vertex : mapped_ports) {
          auto port_slew =
              pick_preserved_clock_pin_slew(port_vertex, preferred_trans_type);
          if (port_slew && *port_slew > 0.0) {
            LOG_INFO_FIRST_N(10)
                << "liberty alignment fallback: reuse source clock port slew "
                << port_vertex->getName() << " -> " << vertex->getName()
                << " slew_ns=" << *port_slew;
            return port_slew;
          }
        }

        return direct_slew;
      };

      auto resolve_input_trans_type =
          [driver_input_path_data, sampled_output_trans_type](StaInstArc* inst_arc) {
            auto output_trans_type = sampled_output_trans_type;
            if (!inst_arc) {
              return driver_input_path_data
                         ? driver_input_path_data->get_trans_type()
                         : output_trans_type;
            }

            // Match OpenROAD's use of the reference arc's source edge when
            // picking the input slew that drives table sampling.
            if (inst_arc->isRisingTriggerArc()) {
              return TransType::kRise;
            }
            if (inst_arc->isFallingTriggerArc()) {
              return TransType::kFall;
            }
            if (inst_arc->isPositiveArc()) {
              return output_trans_type;
            }
            if (inst_arc->isNegativeArc()) {
              return FLIP_TRANS(output_trans_type);
            }
            return driver_input_path_data
                       ? driver_input_path_data->get_trans_type()
                       : output_trans_type;
          };

      auto input_trans_type = resolve_input_trans_type(final_inst_arc);
      auto* reference_input_vertex = final_inst_arc->get_src();
      auto working_slew =
          (delay_data->get_launch_clock_data() && reference_input_vertex &&
           reference_input_vertex->is_clock())
              ? pick_sampling_clock_slew(reference_input_vertex,
                                         input_trans_type)
              : std::nullopt;
      if (!working_slew) {
        working_slew = pick_available_slew(reference_input_vertex,
                                           input_trans_type,
                                           delay_data->get_data_epoch());
      }
      if (!working_slew) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: working slew missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      double actual_load = convertPfToReferenceCapUnit(
          reference_arc, final_inst_arc->get_snk()->getLoad(
                             analysis_mode, block_offset_trans_type));
      double reference_working_delay =
          reference_arc->getDelayOrConstrainCheckNs(block_offset_trans_type,
                                                    *working_slew, actual_load);
      double block_offset_ns = path_delay_ns - reference_working_delay;
      const bool trace_sink =
          delay_data && delay_data->get_own_vertex() &&
          shouldTraceCharacterizationPin(
              delay_data->get_own_vertex()->getName().c_str());
      if (trace_sink) {
        LOG_INFO << "[character_timing][table-build] sink="
                 << delay_data->get_own_vertex()->getName()
                 << " ref_arc=" << final_inst_arc->get_src()->getName() << "->"
                 << final_inst_arc->get_snk()->getName()
                 << " block_offset_trans="
                 << (block_offset_trans_type == TransType::kRise ? "rise"
                                                                 : "fall")
                 << " sampled_output_trans="
                 << (sampled_output_trans_type == TransType::kRise ? "rise"
                                                                   : "fall")
                 << " input_trans="
                 << (input_trans_type == TransType::kRise ? "rise" : "fall")
                 << " working_slew_ns=" << *working_slew
                 << " actual_load=" << actual_load
                 << " path_delay_ns=" << path_delay_ns
                 << " reference_working_delay_ns=" << reference_working_delay
                 << " block_offset_ns=" << block_offset_ns;
      }

      auto* load_template = get_or_create_load_template(*load_axis_info);
      auto delay_table =
          std::make_unique<LibTable>(delay_table_type, load_template);
      delay_table->addAxis(clone_axis(load_axis_info->values));
      auto slew_table = std::make_unique<LibTable>(slew_table_type, load_template);
      slew_table->addAxis(clone_axis(load_axis_info->values));

      for (double sampled_load : load_axis_info->values) {
        double sampled_delay = reference_arc->getDelayOrConstrainCheckNs(
            sampled_output_trans_type, *working_slew, sampled_load);
        double sampled_slew =
            reference_arc->getSlewNs(sampled_output_trans_type, *working_slew,
                                     sampled_load);

        delay_table->addTableValue(
            std::make_unique<LibFloatValue>(block_offset_ns + sampled_delay));
        slew_table->addTableValue(
            std::make_unique<LibFloatValue>(sampled_slew));
      }

      return {std::move(delay_table), std::move(slew_table)};
    };

    auto* ista = Sta::getOrCreateSta();
    auto fall_start_vertex_to_delay_data = getDifferentStartPathDelayDataForEpoch(
        port_vertex, analysis_mode, TransType::kFall, _characterization_epoch);
    auto rise_start_vertex_to_delay_data = getDifferentStartPathDelayDataForEpoch(
        port_vertex, analysis_mode, TransType::kRise, _characterization_epoch);

    struct ClockedOutputArcCandidate {
      std::string related_pin;
      std::string timing_type;
      StaPathDelayData* rise_delay_data = nullptr;
      StaPathDelayData* fall_delay_data = nullptr;
      StaVertex* rise_start_vertex = nullptr;
      StaVertex* fall_start_vertex = nullptr;
      double rise_delay_ns = 0.0;
      double fall_delay_ns = 0.0;
      bool has_rise_delay = false;
      bool has_fall_delay = false;
    };

    struct ClockedOutputTableRequest {
      StaPathDelayData* source_delay_data = nullptr;
      StaVertex* source_start_vertex = nullptr;
      double boundary_delay_ns = 0.0;
      TransType block_offset_trans_type = TransType::kRise;
      TransType sampled_output_trans_type = TransType::kRise;
    };

    std::map<std::pair<std::string, std::string>, ClockedOutputArcCandidate>
        clocked_output_candidates;
    const bool trace_port =
        shouldTraceCharacterizationPin(port_vertex->getName().c_str());
    auto choose_worse_delay = [analysis_mode](double lhs, double rhs) {
      return analysis_mode == AnalysisMode::kMax ? (lhs > rhs) : (lhs < rhs);
    };
    if (ista) {
      auto collect_clocked_output_candidates =
          [&](const auto& start_vertex_to_delay_data,
              TransType output_trans_type) {
            for (const auto& [path_start_vertex, delay_data] :
                 start_vertex_to_delay_data) {
              if (!path_start_vertex || path_start_vertex->is_port() ||
                  !delay_data) {
                continue;
              }

              auto* launch_clock_data = delay_data->get_launch_clock_data();
              if (!launch_clock_data) {
                continue;
              }

              auto related_pin = resolve_clock_related_pin_name(launch_clock_data);
              if (related_pin.empty()) {
                continue;
              }

              auto timing_type =
                  launch_clock_data->get_clock_wave_type() == TransType::kRise
                      ? std::string("rising_edge")
                      : std::string("falling_edge");
              auto candidate_key = std::make_pair(related_pin, timing_type);
              auto& candidate = clocked_output_candidates[candidate_key];
              if (candidate.related_pin.empty()) {
                candidate.related_pin = candidate_key.first;
                candidate.timing_type = candidate_key.second;
              }

              // Match OpenROAD's clocked-output model collection: use the
              // output path arrival relative to the driving clock edge, which
              // is equivalent to data path delay plus launch clock network
              // latency.
              double stored_delay_ns =
                  FS_TO_NS(delay_data->get_arrive_time()) +
                  FS_TO_NS(launch_clock_data->get_arrive_time());
              double raw_delay_ns = rebuildPathDelayNsForEpoch(
                                        delay_data, _characterization_epoch)
                                        .value_or(stored_delay_ns);
              if (trace_port) {
                auto* launch_clock = launch_clock_data->get_prop_clock();
                auto path_nodes = delay_data->getPathData();
                auto* launch_delay_data = delay_data->get_launch_delay_data();
                std::vector<std::string> path_trace;
                while (!path_nodes.empty()) {
                  auto* node = dynamic_cast<StaPathDelayData*>(path_nodes.top());
                  path_nodes.pop();
                  if (!node) {
                    continue;
                  }
                  path_trace.emplace_back(
                      node->get_own_vertex()->getName() + "@" +
                      std::to_string(FS_TO_NS(node->get_arrive_time())));
                }
                LOG_INFO << "[character_timing][clk2out-candidate] port="
                         << port_vertex->getName() << " mode="
                         << (analysis_mode == AnalysisMode::kMax ? "max"
                                                                 : "min")
                         << " related_pin=" << related_pin << " output_trans="
                         << (output_trans_type == TransType::kRise ? "rise"
                                                                   : "fall")
                         << " start_vertex="
                         << (path_start_vertex ? path_start_vertex->getName()
                                               : "null")
                         << " launch_seed_vertex="
                         << (launch_delay_data && launch_delay_data->get_own_vertex()
                                 ? launch_delay_data->get_own_vertex()->getName()
                                 : "null")
                         << " launch_seed_arrive_ns="
                         << (launch_delay_data
                                 ? FS_TO_NS(launch_delay_data->get_arrive_time())
                                 : -1.0)
                         << " path_depth=" << delay_data->getPathData().size()
                         << " stored_arrival_ns="
                         << FS_TO_NS(delay_data->get_arrive_time())
                         << " launch_clk_ns="
                         << FS_TO_NS(launch_clock_data->get_arrive_time())
                         << " rebuilt_boundary_ns=" << raw_delay_ns
                         << " stored_boundary_ns=" << stored_delay_ns
                         << " clock_period_ns="
                         << (launch_clock ? launch_clock->getPeriodNs() : -1.0)
                         << " clock_edge_ns="
                         << (launch_clock_data->get_clock_wave_type() ==
                                     TransType::kRise
                                 ? (launch_clock
                                        ? PS_TO_NS(launch_clock->getRisingEdge())
                                        : -1.0)
                                 : (launch_clock
                                        ? PS_TO_NS(launch_clock->getFallingEdge())
                                        : -1.0))
                         << " path_trace="
                         << [&path_trace]() {
                              std::string joined;
                              for (size_t i = 0; i < path_trace.size(); ++i) {
                                if (i != 0) {
                                  joined.append(" -> ");
                                }
                                joined.append(path_trace[i]);
                              }
                              return joined;
                            }();
              }

              if (output_trans_type == TransType::kRise &&
                  (!candidate.has_rise_delay ||
                   choose_worse_delay(raw_delay_ns, candidate.rise_delay_ns))) {
                candidate.rise_delay_ns = raw_delay_ns;
                candidate.rise_delay_data = delay_data;
                candidate.rise_start_vertex = path_start_vertex;
                candidate.has_rise_delay = true;
              } else if (output_trans_type == TransType::kFall &&
                         (!candidate.has_fall_delay || choose_worse_delay(
                                                          raw_delay_ns,
                                                          candidate.fall_delay_ns))) {
                candidate.fall_delay_ns = raw_delay_ns;
                candidate.fall_delay_data = delay_data;
                candidate.fall_start_vertex = path_start_vertex;
                candidate.has_fall_delay = true;
              }
            }
          };

      collect_clocked_output_candidates(rise_start_vertex_to_delay_data,
                                        TransType::kRise);
      collect_clocked_output_candidates(fall_start_vertex_to_delay_data,
                                        TransType::kFall);
    }

    if (!clocked_output_candidates.empty()) {
      auto choose_table_request = [&](const ClockedOutputArcCandidate& candidate,
                                      TransType table_trans)
          -> std::optional<ClockedOutputTableRequest> {
        if (table_trans == TransType::kRise) {
          if (candidate.has_rise_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.rise_delay_data,
                .source_start_vertex = candidate.rise_start_vertex,
                .boundary_delay_ns = candidate.rise_delay_ns,
                .block_offset_trans_type = TransType::kRise,
                .sampled_output_trans_type = TransType::kRise,
            };
          }
          if (candidate.has_fall_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.fall_delay_data,
                .source_start_vertex = candidate.fall_start_vertex,
                .boundary_delay_ns = candidate.fall_delay_ns,
                .block_offset_trans_type = TransType::kFall,
                .sampled_output_trans_type = TransType::kRise,
            };
          }
        } else {
          if (candidate.has_fall_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.fall_delay_data,
                .source_start_vertex = candidate.fall_start_vertex,
                .boundary_delay_ns = candidate.fall_delay_ns,
                .block_offset_trans_type = TransType::kFall,
                .sampled_output_trans_type = TransType::kFall,
            };
          }
          if (candidate.has_rise_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.rise_delay_data,
                .source_start_vertex = candidate.rise_start_vertex,
                .boundary_delay_ns = candidate.rise_delay_ns,
                .block_offset_trans_type = TransType::kRise,
                .sampled_output_trans_type = TransType::kFall,
            };
          }
        }

        return std::nullopt;
      };

      for (auto& [candidate_key, candidate] : clocked_output_candidates) {
        auto lib_arc = std::make_unique<LibArc>();
        auto delay_model = std::make_unique<LibDelayTableModel>();
        lib_arc->set_snk_port(port_vertex->getName().c_str());
        lib_arc->set_src_port(candidate.related_pin.c_str());
        lib_arc->set_timing_type(candidate.timing_type.c_str());
        lib_arc->set_timing_sense("positive_unate");

        bool has_delay_table = false;
        for (auto table_trans : {TransType::kRise, TransType::kFall}) {
          auto table_request = choose_table_request(candidate, table_trans);
          if (!table_request || !table_request->source_delay_data) {
            continue;
          }

          auto* delay_data = table_request->source_delay_data;
          auto* selected_start_vertex = table_request->source_start_vertex;
          auto* slew_data = findWorstVertexSlewDataForEpoch(
              port_vertex, analysis_mode, table_trans,
              delay_data->get_data_epoch(), selected_start_vertex);
          if (!slew_data) {
            slew_data = findWorstVertexSlewDataForEpoch(
                port_vertex, analysis_mode,
                table_request->block_offset_trans_type,
                delay_data->get_data_epoch(), selected_start_vertex);
          }
          double boundary_delay_ns = table_request->boundary_delay_ns;
          if (trace_port) {
            LOG_INFO << "[character_timing][clk2out-final] port="
                     << port_vertex->getName() << " related_pin="
                     << candidate.related_pin << " output_trans="
                     << (table_trans == TransType::kRise ? "rise" : "fall")
                     << " mode="
                     << (analysis_mode == AnalysisMode::kMax ? "max" : "min")
                     << " start_vertex="
                     << (selected_start_vertex
                             ? selected_start_vertex->getName()
                             : "null")
                     << " boundary_delay_ns=" << boundary_delay_ns
                     << " slew_ns="
                     << (slew_data ? FS_TO_NS(slew_data->get_slew()) : -1.0);
          }
          auto delay_table_type =
              table_trans == TransType::kRise
                  ? LibTable::TableType::kCellRise
                  : LibTable::TableType::kCellFall;
          auto slew_table_type =
              table_trans == TransType::kRise
                  ? LibTable::TableType::kRiseTransition
                  : LibTable::TableType::kFallTransition;
          auto [delay_table, slew_table] = build_table_from_reference(
              delay_data, slew_data, boundary_delay_ns, delay_table_type,
              slew_table_type, table_request->block_offset_trans_type,
              table_request->sampled_output_trans_type);
          delay_model->addTable(std::move(delay_table));
          delay_model->addTable(std::move(slew_table));
          has_delay_table = true;
        }

        if (!has_delay_table) {
          continue;
        }

        lib_arc->set_table_model(std::move(delay_model));
        lib_arc->set_owner_cell(design_timing_cell.get());
        design_timing_cell->addLibertyArc(std::move(lib_arc));
      }
      return;
    }

    std::vector<StaVertex*> all_start_vertexes;
    for (const auto& pair : rise_start_vertex_to_delay_data) {
      all_start_vertexes.emplace_back(pair.first);
    }
    for (const auto& pair : fall_start_vertex_to_delay_data) {
      if (std::find(all_start_vertexes.begin(), all_start_vertexes.end(),
                    pair.first) == all_start_vertexes.end()) {
        all_start_vertexes.emplace_back(pair.first);
      }
    }

    if (all_start_vertexes.empty()) {
      return;
    }

    for (auto* start_vertex : all_start_vertexes) {
      auto lib_arc = std::make_unique<LibArc>();
      auto delay_model = std::make_unique<LibDelayTableModel>();
      auto* representative_delay_data =
          rise_start_vertex_to_delay_data[start_vertex]
              ? rise_start_vertex_to_delay_data[start_vertex]
              : fall_start_vertex_to_delay_data[start_vertex];
      auto related_pin = start_vertex->getName();
      auto timing_type = std::string("combinational");
      if (representative_delay_data) {
        if (auto* launch_clock_data =
                representative_delay_data->get_launch_clock_data();
            launch_clock_data) {
          if (auto clock_related_pin =
                  resolve_clock_related_pin_name(launch_clock_data);
              !clock_related_pin.empty()) {
            related_pin = std::move(clock_related_pin);
          }
          timing_type =
              launch_clock_data->get_clock_wave_type() == TransType::kRise
                  ? "rising_edge"
                  : "falling_edge";
        }
      }

      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_src_port(related_pin.c_str());
      lib_arc->set_timing_type(timing_type.c_str());
      bool has_delay_table = false;
      FOREACH_TRANS(trans) {
        auto* delay_data = trans == TransType::kRise
                               ? rise_start_vertex_to_delay_data[start_vertex]
                               : fall_start_vertex_to_delay_data[start_vertex];
        if (!delay_data) {
          continue;
        }

        auto* slew_data = findWorstVertexSlewDataForEpoch(
            port_vertex, analysis_mode, trans, delay_data->get_data_epoch(),
            start_vertex);
        if (!slew_data) {
          continue;
        }

        auto path_data = delay_data->getPathData();
        const char* timing_sense =
            (trans == path_data.top()->get_trans_type())
                ? "positive_unate"
                : "negative_unate";  // TODO(to taosimin), non-unate should
                                     // consider.
        lib_arc->set_timing_sense(timing_sense);

        auto delay_table_type =
            delay_data->get_trans_type() == TransType::kRise
                ? LibTable::TableType::kCellRise
                : LibTable::TableType::kCellFall;
        auto slew_table_type = slew_data->get_trans_type() == TransType::kRise
                                   ? LibTable::TableType::kRiseTransition
                                   : LibTable::TableType::kFallTransition;
        auto [delay_table, slew_table] = build_table_from_reference(
            delay_data, slew_data, FS_TO_NS(delay_data->get_arrive_time()),
            delay_table_type, slew_table_type, delay_data->get_trans_type(),
            delay_data->get_trans_type());

        delay_model->addTable(std::move(delay_table));
        delay_model->addTable(std::move(slew_table));
        has_delay_table = true;
      }

      if (!has_delay_table) {
        continue;
      }
      lib_arc->set_table_model(std::move(delay_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    }
  };

  auto construct_clock_path_arc = [&design_timing_cell, &build_scalar_table](
                                      auto* port_vertex) {
    auto append_clock_arc = [&](const char* timing_type) {
      auto lib_arc = std::make_unique<LibArc>();
      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_timing_sense("positive_unate");
      lib_arc->set_timing_type(timing_type);

      auto delay_model = std::make_unique<LibDelayTableModel>();
      delay_model->addTable(
          build_scalar_table(LibTable::TableType::kCellRise, 0.0));
      delay_model->addTable(
          build_scalar_table(LibTable::TableType::kCellFall, 0.0));
      lib_arc->set_table_model(std::move(delay_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    };

    append_clock_arc("min_clock_tree_path");
    append_clock_arc("max_clock_tree_path");
  };

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();

    auto lib_port = std::make_unique<LibPort>(the_port->get_name());
    lib_port->set_port_type(resolve_export_port_type(the_port));
    bool is_clock_port =
        port_vertex->is_clock() || _port_to_logic_clkpoint.contains(port_vertex);
    lib_port->set_is_clock(is_clock_port);
    double rise_cap = port_vertex->getLoad(_analysis_mode, TransType::kRise);
    double fall_cap = port_vertex->getLoad(_analysis_mode, TransType::kFall);
    lib_port->set_port_cap(convert_lib_cap_to_internal_pf(
        std::max(rise_cap, fall_cap)));
    design_timing_cell->addLibertyPort(std::move(lib_port));

    if (is_clock_port) {
      construct_clock_path_arc(port_vertex);
    }

    if (the_port->isInput() && !_port_to_logic_clkpoint.contains(port_vertex)) {
      // construct the constrain arc, need know the constrained arc, and the
      // constrain port.
      construct_port_check_arc(port_vertex, _analysis_mode);

    } else if (the_port->isOutput()) {
      // construct the delay arc.
      construct_port_delay_arc(port_vertex, _analysis_mode);
    }
  }

  _design_timing_model->addLibertyCell(std::move(design_timing_cell));

  LOG_INFO << "gen timing model end.";
  return 1;
}

}  // namespace ista
