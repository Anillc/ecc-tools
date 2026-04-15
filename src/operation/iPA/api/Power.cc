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
 * @file Power.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The top class of power analysis, should include the api wrapper etc.
 * @version 0.1
 * @date 2023-01-02
 */

#include "Power.hh"

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "json/json.hpp"
#include "ops/annotate_toggle_sp/AnnotateToggleSP.hh"
#include "ops/build_graph/PwrBuildGraph.hh"
#include "ops/calc_power/PwrCalcInternalPower.hh"
#include "ops/calc_power/PwrCalcLeakagePower.hh"
#include "ops/calc_power/PwrCalcSwitchPower.hh"
#include "ops/dump/PwrDumpGraph.hh"
#include "ops/dump/PwrDumpSeqGraph.hh"
#include "ops/levelize_seq_graph/PwrBuildSeqGraph.hh"
#include "ops/levelize_seq_graph/PwrCheckPipelineLoop.hh"
#include "ops/levelize_seq_graph/PwrLevelizeSeqGraph.hh"
#include "ops/plot_power/PwrReport.hh"
#include "ops/plot_power/PwrReportInstance.hh"
#include "ops/propagate_toggle_sp/PwrPropagateClock.hh"
#include "ops/propagate_toggle_sp/PwrPropagateConst.hh"
#include "ops/propagate_toggle_sp/PwrPropagateToggleSP.hh"

namespace ipower {

struct icts_char::InternalPowerContext {
  struct PreparedWhenExpr {
    RustLibertyExprOp op = RustLibertyExprOp::kZero;
    PwrVertex* bound_vertex = nullptr;
    int bound_vertex_sample_index = -1;
    int sample_eval_index = -1;
    std::unique_ptr<PreparedWhenExpr> left;
    std::unique_ptr<PreparedWhenExpr> right;
  };

  struct PreparedPinPowerTerm {
    LibInternalPowerInfo* internal_power = nullptr;
    std::unique_ptr<PreparedWhenExpr> when_expr;
  };

  struct PreparedOutputArcTerm {
    LibPowerArc* power_arc = nullptr;
    std::unique_ptr<PreparedWhenExpr> when_expr;
  };

  struct PreparedOutputArcContext {
    PwrInstArc* inst_arc = nullptr;
    StaArc* sta_arc = nullptr;
    int src_sta_vertex_sample_index = -1;
    int snk_sta_vertex_sample_index = -1;
    bool is_positive_arc = true;
    std::vector<PreparedOutputArcTerm> terms;
  };

  struct PreparedPinContext {
    Pin* pin = nullptr;
    LibPort* cell_port = nullptr;
    LibCell* lib_cell = nullptr;
    StaVertex* sta_vertex = nullptr;
    PwrVertex* pwr_vertex = nullptr;
    int sta_vertex_sample_index = -1;
    int pwr_vertex_sample_index = -1;
    StaVertex* output_sta_vertex = nullptr;
    PwrVertex* output_pwr_vertex = nullptr;
    std::vector<PreparedPinPowerTerm> pin_power_terms;
    std::vector<PreparedOutputArcContext> output_arc_contexts;
  };

  struct PreparedCellContext {
    Instance* inst = nullptr;
    bool is_seq = false;
    std::vector<PreparedPinContext> pins;
  };

  std::vector<PreparedCellContext> cells;
  std::vector<PwrVertex*> touched_vertices;
  std::vector<PwrInstArc*> touched_inst_arcs;
  std::vector<PwrVertex*> sample_pwr_vertices;
  std::vector<StaVertex*> sample_sta_vertices;
  std::vector<double> sample_toggle_values;
  std::vector<double> sample_sp_values;
  std::vector<double> sample_rise_slew_values;
  std::vector<double> sample_fall_slew_values;
  std::vector<unsigned char> sample_has_rise_slew;
  std::vector<unsigned char> sample_has_fall_slew;
  std::vector<unsigned char> sample_has_slew_bucket;
  std::vector<double> sample_rise_load_values;
  std::vector<double> sample_fall_load_values;
  std::vector<double> sample_when_values;
  std::vector<unsigned char> sample_when_ready;
  int sample_when_expr_count = 0;
  bool has_prepared_sample_context = false;
  bool has_frozen_power_sample_context = false;
};

namespace {

using InternalPowerContext = icts_char::InternalPowerContext;
using PreparedCellContext = InternalPowerContext::PreparedCellContext;
using PreparedOutputArcContext =
    InternalPowerContext::PreparedOutputArcContext;
using PreparedOutputArcTerm =
    InternalPowerContext::PreparedOutputArcTerm;
using PreparedPinContext = InternalPowerContext::PreparedPinContext;
using PreparedPinPowerTerm = InternalPowerContext::PreparedPinPowerTerm;
using PreparedWhenExpr = InternalPowerContext::PreparedWhenExpr;

auto propagateCharClockVertex(PwrGraph& power_graph, StaClock* propagated_clock,
                              PwrVertex* the_vertex) -> unsigned {
  LOG_FATAL_IF(propagated_clock == nullptr)
      << "characterization propagated clock is null.";
  LOG_FATAL_IF(the_vertex == nullptr)
      << "characterization clock vertex is null.";

  the_vertex->set_is_clock_network();
  auto* fastest_clock = &(power_graph.get_fastest_clock());
  the_vertex->addData(c_default_clock_toggle / propagated_clock->getPeriodNs(),
                      c_default_clock_sp, PwrDataSource::kClockPropagation,
                      fastest_clock);

  if (the_vertex->get_sta_vertex()->is_clock()) {
    return 1;
  }

  FOREACH_SRC_PWR_ARC(the_vertex, the_arc) {
    auto* the_snk_vertex = the_arc->get_snk();
    if (propagateCharClockVertex(power_graph, propagated_clock,
                                 the_snk_vertex) == 0U) {
      return 0;
    }
  }

  return 1;
}

auto prepareCharClockPowerData(Power& power) -> unsigned {
  if (power.checkPipelineLoop() == 0U) {
    return 0;
  }
  if (power.levelizeSeqGraph() == 0U) {
    return 0;
  }
  if (power.propagateConst() == 0U) {
    return 0;
  }

  auto& power_graph = power.get_power_graph();
  for (auto* sta_clock : power_graph.get_sta_clocks()) {
    if (sta_clock == nullptr) {
      continue;
    }

    auto& clock_vertexes = sta_clock->get_clock_vertexes();
    for (auto* clock_vertex : clock_vertexes) {
      auto* pwr_clock_vertex = power_graph.staToPwrVertex(clock_vertex);
      if (pwr_clock_vertex == nullptr) {
        continue;
      }
      if (propagateCharClockVertex(power_graph, sta_clock, pwr_clock_vertex) ==
          0U) {
        return 0;
      }
    }
  }

  return 1;
}

void resetLeakageGroupData(
    std::map<DesignObject*, std::unique_ptr<PwrGroupData>>& obj_to_datas) {
  for (auto& [design_obj, group_data] : obj_to_datas) {
    (void)design_obj;
    if (group_data) {
      group_data->set_leakage_power(0.0);
    }
  }
}

void resetInternalGroupData(
    std::map<DesignObject*, std::unique_ptr<PwrGroupData>>& obj_to_datas) {
  for (auto& [design_obj, group_data] : obj_to_datas) {
    (void)design_obj;
    if (group_data) {
      group_data->set_internal_power(0.0);
    }
  }
}

void resetSwitchGroupData(
    std::map<DesignObject*, std::unique_ptr<PwrGroupData>>& obj_to_datas) {
  for (auto& [design_obj, group_data] : obj_to_datas) {
    (void)design_obj;
    if (group_data) {
      group_data->set_switch_power(0.0);
    }
  }
}

void resetGraphInternalPower(PwrGraph& power_graph) {
  PwrVertex* pwr_vertex;
  FOREACH_PWR_VERTEX(&power_graph, pwr_vertex) {
    pwr_vertex->set_internal_power(0.0);
  }

  PwrArc* pwr_arc;
  FOREACH_PWR_ARC((&power_graph), pwr_arc) {
    if (pwr_arc->isInstArc()) {
      dynamic_cast<PwrInstArc*>(pwr_arc)->set_internal_power(0.0);
    }
  }
}

auto prepareWhenExpr(
    RustLibertyExpr* expr,
    const std::unordered_map<std::string, PwrVertex*>& pin_name_to_vertex)
    -> std::unique_ptr<PreparedWhenExpr> {
  if (expr == nullptr) {
    return nullptr;
  }

  auto prepared_expr = std::make_unique<PreparedWhenExpr>();
  prepared_expr->op = expr->op;
  if (expr->op == RustLibertyExprOp::kBuffer) {
    const std::string port_name = expr->port_name ? expr->port_name : "";
    const auto vertex_iter = pin_name_to_vertex.find(port_name);
    LOG_FATAL_IF(vertex_iter == pin_name_to_vertex.end())
        << "not found prepared power vertex for pin " << port_name;
    prepared_expr->bound_vertex = vertex_iter->second;
  }

  auto* left_expr = rust_get_expr_left(expr);
  auto* right_expr = rust_get_expr_right(expr);
  prepared_expr->left = prepareWhenExpr(left_expr, pin_name_to_vertex);
  prepared_expr->right = prepareWhenExpr(right_expr, pin_name_to_vertex);

  if (left_expr != nullptr) {
    rust_free_expr(left_expr);
  }
  if (right_expr != nullptr) {
    rust_free_expr(right_expr);
  }

  return prepared_expr;
}

auto prepareWhenExpr(
    const std::string& when,
    const std::unordered_map<std::string, PwrVertex*>& pin_name_to_vertex)
    -> std::unique_ptr<PreparedWhenExpr> {
  if (when.empty()) {
    return nullptr;
  }

  RustLibertyExprBuilder expr_builder(when.c_str());
  expr_builder.execute();
  auto* expr = expr_builder.get_result_expr();
  auto prepared_expr = prepareWhenExpr(expr, pin_name_to_vertex);
  if (expr != nullptr) {
    rust_free_expr(expr);
  }
  return prepared_expr;
}

auto evalPreparedWhenExpr(const PreparedWhenExpr* expr) -> double {
  LOG_FATAL_IF(expr == nullptr) << "prepared power expression is null.";

  switch (expr->op) {
    case RustLibertyExprOp::kBuffer:
      LOG_FATAL_IF(expr->bound_vertex == nullptr)
          << "prepared power leaf vertex is null.";
      return expr->bound_vertex->getSPData(std::nullopt);
    case RustLibertyExprOp::kOne:
      return 1.0;
    case RustLibertyExprOp::kZero:
      return 0.0;
    case RustLibertyExprOp::kNot:
      return 1.0 - evalPreparedWhenExpr(expr->left.get());
    case RustLibertyExprOp::kOr: {
      const double left_sp = evalPreparedWhenExpr(expr->left.get());
      const double right_sp = evalPreparedWhenExpr(expr->right.get());
      return 1.0 - (1.0 - left_sp) * (1.0 - right_sp);
    }
    case RustLibertyExprOp::kMult:
    case RustLibertyExprOp::kAnd:
      return evalPreparedWhenExpr(expr->left.get()) *
             evalPreparedWhenExpr(expr->right.get());
    case RustLibertyExprOp::kPlus:
    case RustLibertyExprOp::kXor: {
      const double left_sp = evalPreparedWhenExpr(expr->left.get());
      const double right_sp = evalPreparedWhenExpr(expr->right.get());
      return left_sp * (1.0 - right_sp) + (1.0 - left_sp) * right_sp;
    }
  }

  return 0.0;
}

auto getPreparedToggle(PwrVertex* pwr_vertex) -> double {
  LOG_FATAL_IF(pwr_vertex == nullptr) << "prepared power vertex is null.";
  return pwr_vertex->getToggleData(std::nullopt);
}

auto convertLoadToLibUnit(LibPowerArc* power_arc, double load_pf) -> double {
  auto* the_lib = power_arc->get_owner_cell()->get_owner_lib();

  double load = 0.0;
  if (the_lib->get_cap_unit() == CapacitiveUnit::kFF) {
    load = PF_TO_FF(load_pf);
  } else if (the_lib->get_cap_unit() == CapacitiveUnit::kPF) {
    load = load_pf;
  }

  return load;
}

auto registerSamplePwrVertex(
    PwrVertex* pwr_vertex, std::unordered_map<PwrVertex*, int>& vertex_to_index,
    std::vector<PwrVertex*>& sample_vertices) -> int {
  LOG_FATAL_IF(pwr_vertex == nullptr)
      << "prepared sample power vertex is null.";
  const auto [iter, inserted] = vertex_to_index.emplace(
      pwr_vertex, static_cast<int>(sample_vertices.size()));
  if (inserted) {
    sample_vertices.emplace_back(pwr_vertex);
  }
  return iter->second;
}

auto registerSampleStaVertex(
    StaVertex* sta_vertex, std::unordered_map<StaVertex*, int>& vertex_to_index,
    std::vector<StaVertex*>& sample_vertices) -> int {
  LOG_FATAL_IF(sta_vertex == nullptr) << "prepared sample STA vertex is null.";
  const auto [iter, inserted] = vertex_to_index.emplace(
      sta_vertex, static_cast<int>(sample_vertices.size()));
  if (inserted) {
    sample_vertices.emplace_back(sta_vertex);
  }
  return iter->second;
}

void indexPreparedWhenExpr(PreparedWhenExpr* expr,
                           std::unordered_map<PwrVertex*, int>& pwr_to_index,
                           std::vector<PwrVertex*>& sample_pwr_vertices,
                           int& when_expr_count) {
  if (expr == nullptr) {
    return;
  }

  expr->sample_eval_index = when_expr_count++;
  if (expr->op == RustLibertyExprOp::kBuffer) {
    LOG_FATAL_IF(expr->bound_vertex == nullptr)
        << "prepared power leaf vertex is null.";
    expr->bound_vertex_sample_index = registerSamplePwrVertex(
        expr->bound_vertex, pwr_to_index, sample_pwr_vertices);
  }

  indexPreparedWhenExpr(expr->left.get(), pwr_to_index, sample_pwr_vertices,
                        when_expr_count);
  indexPreparedWhenExpr(expr->right.get(), pwr_to_index, sample_pwr_vertices,
                        when_expr_count);
}

void refreshPreparedInternalPowerPowerSampleSnapshot(
    InternalPowerContext& prepared_context) {
  const auto pwr_vertex_num = prepared_context.sample_pwr_vertices.size();
  for (size_t idx = 0; idx < pwr_vertex_num; ++idx) {
    auto* pwr_vertex = prepared_context.sample_pwr_vertices[idx];
    LOG_FATAL_IF(pwr_vertex == nullptr)
        << "prepared sample power vertex is null.";
    prepared_context.sample_toggle_values[idx] =
        pwr_vertex->getToggleData(std::nullopt);
    prepared_context.sample_sp_values[idx] =
        pwr_vertex->getSPData(std::nullopt);
  }
}

void refreshPreparedInternalPowerStaSampleSnapshot(
    InternalPowerContext& prepared_context) {
  const auto sta_vertex_num = prepared_context.sample_sta_vertices.size();
  for (size_t idx = 0; idx < sta_vertex_num; ++idx) {
    auto* sta_vertex = prepared_context.sample_sta_vertices[idx];
    LOG_FATAL_IF(sta_vertex == nullptr)
        << "prepared sample STA vertex is null.";

    auto rise_slew =
        sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kRise);
    auto fall_slew =
        sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kFall);
    prepared_context.sample_has_rise_slew[idx] = rise_slew.has_value();
    prepared_context.sample_has_fall_slew[idx] = fall_slew.has_value();
    prepared_context.sample_rise_slew_values[idx] = rise_slew.value_or(0.0);
    prepared_context.sample_fall_slew_values[idx] = fall_slew.value_or(0.0);
    prepared_context.sample_has_slew_bucket[idx] =
        sta_vertex->getSlewBucket().empty() ? 0U : 1U;
    prepared_context.sample_rise_load_values[idx] =
        sta_vertex->getLoad(AnalysisMode::kMax, TransType::kRise);
    prepared_context.sample_fall_load_values[idx] =
        sta_vertex->getLoad(AnalysisMode::kMax, TransType::kFall);
  }
}

void refreshPreparedInternalPowerSampleSnapshot(
    InternalPowerContext& prepared_context) {
  refreshPreparedInternalPowerPowerSampleSnapshot(prepared_context);
  refreshPreparedInternalPowerStaSampleSnapshot(prepared_context);
  std::fill(prepared_context.sample_when_ready.begin(),
            prepared_context.sample_when_ready.end(), 0);
}

auto getPreparedSampleToggle(
    const PreparedPinContext& pin_context,
    const InternalPowerContext& prepared_context) -> double {
  const int pwr_index = pin_context.pwr_vertex_sample_index;
  LOG_FATAL_IF(pwr_index < 0 ||
               pwr_index >= static_cast<int>(
                                prepared_context.sample_toggle_values.size()))
      << "prepared sample power vertex index is invalid.";
  return prepared_context.sample_toggle_values[pwr_index];
}

auto evalPreparedWhenExprBySample(
    const PreparedWhenExpr* expr,
    InternalPowerContext& prepared_context) -> double {
  LOG_FATAL_IF(expr == nullptr) << "prepared power expression is null.";
  const int eval_index = expr->sample_eval_index;
  LOG_FATAL_IF(eval_index < 0 ||
               eval_index >= prepared_context.sample_when_expr_count)
      << "prepared sample expression index is invalid.";

  auto& ready_bucket = prepared_context.sample_when_ready;
  auto& value_bucket = prepared_context.sample_when_values;
  if (ready_bucket[eval_index] != 0U) {
    return value_bucket[eval_index];
  }

  double eval_value = 0.0;
  switch (expr->op) {
    case RustLibertyExprOp::kBuffer: {
      const int pwr_index = expr->bound_vertex_sample_index;
      LOG_FATAL_IF(pwr_index < 0 ||
                   pwr_index >= static_cast<int>(
                                    prepared_context.sample_sp_values.size()))
          << "prepared sample expression power index is invalid.";
      eval_value = prepared_context.sample_sp_values[pwr_index];
      break;
    }
    case RustLibertyExprOp::kOne:
      eval_value = 1.0;
      break;
    case RustLibertyExprOp::kZero:
      eval_value = 0.0;
      break;
    case RustLibertyExprOp::kNot:
      eval_value = 1.0 - evalPreparedWhenExprBySample(expr->left.get(),
                                                      prepared_context);
      break;
    case RustLibertyExprOp::kOr: {
      const double left_sp =
          evalPreparedWhenExprBySample(expr->left.get(), prepared_context);
      const double right_sp =
          evalPreparedWhenExprBySample(expr->right.get(), prepared_context);
      eval_value = 1.0 - (1.0 - left_sp) * (1.0 - right_sp);
      break;
    }
    case RustLibertyExprOp::kMult:
    case RustLibertyExprOp::kAnd:
      eval_value =
          evalPreparedWhenExprBySample(expr->left.get(), prepared_context) *
          evalPreparedWhenExprBySample(expr->right.get(), prepared_context);
      break;
    case RustLibertyExprOp::kPlus:
    case RustLibertyExprOp::kXor: {
      const double left_sp =
          evalPreparedWhenExprBySample(expr->left.get(), prepared_context);
      const double right_sp =
          evalPreparedWhenExprBySample(expr->right.get(), prepared_context);
      eval_value = left_sp * (1.0 - right_sp) + (1.0 - left_sp) * right_sp;
      break;
    }
  }

  ready_bucket[eval_index] = 1U;
  value_bucket[eval_index] = eval_value;
  return eval_value;
}

void primePreparedWhenExprSampleCache(InternalPowerContext& prepared_context) {
  for (auto& cell_context : prepared_context.cells) {
    for (auto& pin_context : cell_context.pins) {
      for (auto& power_term : pin_context.pin_power_terms) {
        if (power_term.when_expr != nullptr) {
          (void) evalPreparedWhenExprBySample(power_term.when_expr.get(),
                                              prepared_context);
        }
      }

      for (auto& arc_context : pin_context.output_arc_contexts) {
        for (auto& power_term : arc_context.terms) {
          if (power_term.when_expr != nullptr) {
            (void) evalPreparedWhenExprBySample(power_term.when_expr.get(),
                                                prepared_context);
          }
        }
      }
    }
  }
}

auto calcPreparedCombInputPinPowerBySample(
    const PreparedPinContext& pin_context, double input_sum_toggle,
    double output_pin_toggle, InternalPowerContext& prepared_context)
    -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.cell_port == nullptr)
      << "prepared comb-input cell port is null.";
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared comb-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared comb-input STA vertex is null.";

  const int sta_index = pin_context.sta_vertex_sample_index;
  LOG_FATAL_IF(sta_index < 0 ||
               sta_index >= static_cast<int>(
                                prepared_context.sample_has_rise_slew.size()))
      << "prepared sample STA vertex index is invalid.";
  const bool has_rise_slew =
      prepared_context.sample_has_rise_slew[sta_index] != 0U;
  const bool has_fall_slew =
      prepared_context.sample_has_fall_slew[sta_index] != 0U;
  const double rise_slew_ns =
      prepared_context.sample_rise_slew_values[sta_index];
  const double fall_slew_ns =
      prepared_context.sample_fall_slew_values[sta_index];
  const double input_pin_toggle =
      getPreparedSampleToggle(pin_context, prepared_context);

  for (const auto& power_term : pin_context.pin_power_terms) {
    if (!has_rise_slew) {
      LOG_ERROR << pin_context.sta_vertex->getName()
                << " rise slew is not exist.";
      continue;
    }

    const double rise_power = power_term.internal_power->gatePower(
        TransType::kRise, rise_slew_ns, std::nullopt);
    const double rise_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(rise_power);

    LOG_FATAL_IF(!has_fall_slew)
        << pin_context.sta_vertex->getName() << " fall slew is not exist.";
    const double fall_power = power_term.internal_power->gatePower(
        TransType::kFall, fall_slew_ns, std::nullopt);
    const double fall_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(fall_power);

    const double output_flip_toggle =
        output_pin_toggle > 0.0
            ? (input_pin_toggle / input_sum_toggle) * output_pin_toggle
            : 0.0;
    const double output_no_flip_toggle = input_pin_toggle - output_flip_toggle;
    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);
    const double internal_power = output_no_flip_toggle * average_power_mw;

    if (power_term.when_expr != nullptr) {
      pin_internal_power += evalPreparedWhenExprBySample(
                                power_term.when_expr.get(), prepared_context) *
                            internal_power;
    } else {
      pin_internal_power += internal_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedOutputPinPowerBySample(
    const PreparedPinContext& pin_context,
    InternalPowerContext& prepared_context,
    bool is_write_internal_power) -> double {
  double pin_internal_power = 0.0;
  const double output_toggle =
      getPreparedSampleToggle(pin_context, prepared_context);

  for (const auto& arc_context : pin_context.output_arc_contexts) {
    LOG_FATAL_IF(arc_context.sta_arc == nullptr)
        << "prepared output STA arc is null.";
    const int src_sta_index = arc_context.src_sta_vertex_sample_index;
    const int snk_sta_index = arc_context.snk_sta_vertex_sample_index;
    LOG_FATAL_IF(
        src_sta_index < 0 ||
        src_sta_index >=
            static_cast<int>(prepared_context.sample_rise_slew_values.size()))
        << "prepared output source STA index is invalid.";
    LOG_FATAL_IF(
        snk_sta_index < 0 ||
        snk_sta_index >=
            static_cast<int>(prepared_context.sample_rise_load_values.size()))
        << "prepared output sink STA index is invalid.";

    for (const auto& power_term : arc_context.terms) {
      auto query_power = [&arc_context, &prepared_context, &power_term,
                          src_sta_index,
                          snk_sta_index](TransType trans_type) -> double {
        auto* internal_power_info =
            power_term.power_arc->get_internal_power_info().get();
        const TransType src_trans_type =
            arc_context.is_positive_arc ? trans_type : FLIP_TRANS(trans_type);
        const bool has_input_slew =
            src_trans_type == TransType::kRise
                ? prepared_context.sample_has_rise_slew[src_sta_index] != 0U
                : prepared_context.sample_has_fall_slew[src_sta_index] != 0U;
        const double input_slew_ns =
            src_trans_type == TransType::kRise
                ? prepared_context.sample_rise_slew_values[src_sta_index]
                : prepared_context.sample_fall_slew_values[src_sta_index];
        LOG_ERROR_IF_EVERY_N(!has_input_slew, 10)
            << arc_context.sta_arc->get_src()->getName()
            << " input slew is not exist.";

        const double output_load_pf =
            trans_type == TransType::kRise
                ? prepared_context.sample_rise_load_values[snk_sta_index]
                : prepared_context.sample_fall_load_values[snk_sta_index];
        const double output_load =
            convertLoadToLibUnit(power_term.power_arc, output_load_pf);
        const double internal_power_value = internal_power_info->gatePower(
            trans_type, input_slew_ns, output_load);
        return power_term.power_arc->get_owner_cell()->convertTablePowerToMw(
            internal_power_value);
      };

      const double rise_power_mw = query_power(TransType::kRise);
      const double fall_power_mw = query_power(TransType::kFall);
      double arc_power =
          output_toggle * CalcAveragePower(rise_power_mw, fall_power_mw);

      if (power_term.when_expr != nullptr) {
        arc_power *= evalPreparedWhenExprBySample(power_term.when_expr.get(),
                                                  prepared_context);
      }

      pin_internal_power += arc_power;
      if (is_write_internal_power) {
        arc_context.inst_arc->set_internal_power(arc_power);
      }
    }
  }

  return pin_internal_power;
}

auto calcPreparedClockPinPowerBySample(
    const PreparedPinContext& pin_context, double output_pin_toggle,
    InternalPowerContext& prepared_context) -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared clock-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared clock-input STA vertex is null.";

  const int sta_index = pin_context.sta_vertex_sample_index;
  LOG_FATAL_IF(sta_index < 0 ||
               sta_index >= static_cast<int>(
                                prepared_context.sample_has_rise_slew.size()))
      << "prepared sample STA vertex index is invalid.";
  const bool has_rise_slew =
      prepared_context.sample_has_rise_slew[sta_index] != 0U;
  const bool has_fall_slew =
      prepared_context.sample_has_fall_slew[sta_index] != 0U;
  const double rise_slew_ns =
      prepared_context.sample_rise_slew_values[sta_index];
  const double fall_slew_ns =
      prepared_context.sample_fall_slew_values[sta_index];
  const double clock_pin_toggle =
      getPreparedSampleToggle(pin_context, prepared_context);

  for (const auto& power_term : pin_context.pin_power_terms) {
    if (!has_rise_slew) {
      LOG_ERROR << pin_context.sta_vertex->getName()
                << " rise slew is not exist.";
    }
    if (!has_fall_slew) {
      LOG_ERROR << pin_context.sta_vertex->getName()
                << " fall slew is not exist.";
    }

    const double rise_power = power_term.internal_power->gatePower(
        TransType::kRise, has_rise_slew ? rise_slew_ns : 0.0, std::nullopt);
    const double rise_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(rise_power);

    const double fall_power = power_term.internal_power->gatePower(
        TransType::kFall, has_fall_slew ? fall_slew_ns : 0.0, std::nullopt);
    const double fall_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(fall_power);
    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);

    const double output_flip_power =
        HalfToggle(clock_pin_toggle) *
        (pin_context.sta_vertex->isRisingTriggered() ? rise_power_mw
                                                     : fall_power_mw);
    const double output_no_flip_power =
        (clock_pin_toggle - output_pin_toggle) * average_power_mw +
        HalfToggle(clock_pin_toggle) *
            (pin_context.sta_vertex->isRisingTriggered() ? rise_power_mw
                                                         : fall_power_mw);

    if (power_term.when_expr != nullptr) {
      pin_internal_power += evalPreparedWhenExprBySample(
                                power_term.when_expr.get(), prepared_context) *
                            (output_flip_power + output_no_flip_power);
    } else {
      pin_internal_power += output_flip_power + output_no_flip_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedSeqInputPinPowerBySample(
    const PreparedPinContext& pin_context,
    InternalPowerContext& prepared_context) -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared seq-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared seq-input STA vertex is null.";

  const int sta_index = pin_context.sta_vertex_sample_index;
  LOG_FATAL_IF(sta_index < 0 ||
               sta_index >= static_cast<int>(
                                prepared_context.sample_has_rise_slew.size()))
      << "prepared sample STA vertex index is invalid.";
  if (prepared_context.sample_has_slew_bucket[sta_index] == 0U) {
    return 0.0;
  }
  const bool has_rise_slew =
      prepared_context.sample_has_rise_slew[sta_index] != 0U;
  const bool has_fall_slew =
      prepared_context.sample_has_fall_slew[sta_index] != 0U;
  const double rise_slew_ns =
      prepared_context.sample_rise_slew_values[sta_index];
  const double fall_slew_ns =
      prepared_context.sample_fall_slew_values[sta_index];
  const double input_pin_toggle =
      getPreparedSampleToggle(pin_context, prepared_context);

  for (const auto& power_term : pin_context.pin_power_terms) {
    double rise_power_mw = 0.0;
    LOG_ERROR_IF(!has_rise_slew)
        << pin_context.sta_vertex->getName() << " rise slew is not exist.";
    if (has_rise_slew) {
      const double rise_power = power_term.internal_power->gatePower(
          TransType::kRise, rise_slew_ns, std::nullopt);
      rise_power_mw = pin_context.lib_cell->convertTablePowerToMw(rise_power);
    }

    LOG_ERROR_IF(!has_fall_slew)
        << pin_context.sta_vertex->getName() << " fall slew is not exist.";
    double fall_power_mw = rise_power_mw;
    if (has_fall_slew) {
      const double fall_power = power_term.internal_power->gatePower(
          TransType::kFall, fall_slew_ns, std::nullopt);
      fall_power_mw = pin_context.lib_cell->convertTablePowerToMw(fall_power);
    }

    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);
    const double internal_power = input_pin_toggle * average_power_mw;

    if (power_term.when_expr != nullptr) {
      pin_internal_power += evalPreparedWhenExprBySample(
                                power_term.when_expr.get(), prepared_context) *
                            internal_power;
    } else {
      pin_internal_power += internal_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedCombInternalPowerBySample(
    const PreparedCellContext& cell_context,
    InternalPowerContext& prepared_context,
    bool is_write_internal_power) -> double {
  double inst_internal_power = 0.0;
  double input_sum_toggle = 0.0;
  double output_pin_toggle = 0.0;

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pin->isInput()) {
      input_sum_toggle +=
          getPreparedSampleToggle(pin_context, prepared_context);
    } else {
      output_pin_toggle =
          getPreparedSampleToggle(pin_context, prepared_context);
    }
  }

  for (const auto& pin_context : cell_context.pins) {
    double pin_internal_power = 0.0;
    if (pin_context.pin->isInput()) {
      pin_internal_power = calcPreparedCombInputPinPowerBySample(
          pin_context, input_sum_toggle, output_pin_toggle, prepared_context);
      if (is_write_internal_power) {
        pin_context.pwr_vertex->set_internal_power(pin_internal_power);
      }
      inst_internal_power += pin_internal_power;
    }

    if (pin_context.pin->isOutput()) {
      if (pin_context.pin->get_net()) {
        pin_internal_power = calcPreparedOutputPinPowerBySample(
            pin_context, prepared_context, is_write_internal_power);
        inst_internal_power += pin_internal_power;
      }
    }
  }

  return inst_internal_power;
}

auto calcPreparedSeqInternalPowerBySample(
    const PreparedCellContext& cell_context,
    InternalPowerContext& prepared_context,
    bool is_write_internal_power) -> double {
  double inst_internal_power = 0.0;
  double output_pin_toggle = 0.0;

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pin->isOutput()) {
      output_pin_toggle =
          getPreparedSampleToggle(pin_context, prepared_context);
    }
  }

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pwr_vertex->is_const()) {
      continue;
    }

    if (pin_context.pin->isInput()) {
      double pin_internal_power = 0.0;
      if (pin_context.sta_vertex->is_clock()) {
        pin_internal_power = calcPreparedClockPinPowerBySample(
            pin_context, output_pin_toggle, prepared_context);
      } else {
        pin_internal_power =
            calcPreparedSeqInputPinPowerBySample(pin_context, prepared_context);
      }

      inst_internal_power += pin_internal_power;
      if (is_write_internal_power) {
        pin_context.pwr_vertex->set_internal_power(pin_internal_power);
      }
    } else {
      inst_internal_power += calcPreparedOutputPinPowerBySample(
          pin_context, prepared_context, is_write_internal_power);
    }
  }

  return inst_internal_power;
}

auto calcPreparedInternalPowerTotalBySample(
    InternalPowerContext& prepared_context,
    bool is_write_internal_power,
    std::vector<double>* per_cell_internal_power_mw = nullptr) -> double {
  double internal_power_result = 0.0;
  if (per_cell_internal_power_mw != nullptr) {
    per_cell_internal_power_mw->clear();
    per_cell_internal_power_mw->reserve(prepared_context.cells.size());
  }
  for (const auto& cell_context : prepared_context.cells) {
    auto* inst_cell = cell_context.inst->get_inst_cell();
    double inst_internal_power = 0.0;

    if (inst_cell->isMacroCell()) {
      // TODO
    } else if (cell_context.is_seq) {
      inst_internal_power = calcPreparedSeqInternalPowerBySample(
          cell_context, prepared_context, is_write_internal_power);
    } else {
      inst_internal_power = calcPreparedCombInternalPowerBySample(
          cell_context, prepared_context, is_write_internal_power);
    }
    if (per_cell_internal_power_mw != nullptr) {
      per_cell_internal_power_mw->emplace_back(inst_internal_power);
    }
    internal_power_result += inst_internal_power;
  }
  return internal_power_result;
}

auto calcPreparedCombInputPinPower(const PreparedPinContext& pin_context,
                                   double input_sum_toggle,
                                   double output_pin_toggle) -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.cell_port == nullptr)
      << "prepared comb-input cell port is null.";
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared comb-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared comb-input STA vertex is null.";

  for (const auto& power_term : pin_context.pin_power_terms) {
    auto rise_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kRise);
    if (!rise_slew) {
      LOG_ERROR_IF(!rise_slew)
          << pin_context.sta_vertex->getName() << " rise slew is not exist.";
      continue;
    }

    const double rise_power = power_term.internal_power->gatePower(
        TransType::kRise, rise_slew.value_or(0.0), std::nullopt);
    const double rise_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(rise_power);

    auto fall_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kFall);
    LOG_FATAL_IF(!fall_slew)
        << pin_context.sta_vertex->getName() << " fall slew is not exist.";
    const double fall_power = power_term.internal_power->gatePower(
        TransType::kFall, fall_slew.value_or(0.0), std::nullopt);
    const double fall_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(fall_power);

    const double input_pin_toggle = getPreparedToggle(pin_context.pwr_vertex);
    const double output_flip_toggle =
        output_pin_toggle > 0.0
            ? (input_pin_toggle / input_sum_toggle) * output_pin_toggle
            : 0.0;
    const double output_no_flip_toggle = input_pin_toggle - output_flip_toggle;
    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);
    const double internal_power = output_no_flip_toggle * average_power_mw;

    if (power_term.when_expr != nullptr) {
      pin_internal_power +=
          evalPreparedWhenExpr(power_term.when_expr.get()) * internal_power;
    } else {
      pin_internal_power += internal_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedOutputPinPower(const PreparedPinContext& pin_context)
    -> double {
  double pin_internal_power = 0.0;
  const double output_toggle = getPreparedToggle(pin_context.pwr_vertex);

  for (const auto& arc_context : pin_context.output_arc_contexts) {
    LOG_FATAL_IF(arc_context.sta_arc == nullptr)
        << "prepared output STA arc is null.";
    for (const auto& power_term : arc_context.terms) {
      auto query_power = [&arc_context,
                          &power_term](TransType trans_type) -> double {
        auto* internal_power_info =
            power_term.power_arc->get_internal_power_info().get();
        auto input_slew_ns = arc_context.sta_arc->get_src()->getSlewNs(
            AnalysisMode::kMax, arc_context.sta_arc->isPositiveArc()
                                    ? trans_type
                                    : FLIP_TRANS(trans_type));
        LOG_ERROR_IF_EVERY_N(!input_slew_ns, 10)
            << arc_context.sta_arc->get_src()->getName()
            << " input slew is not exist.";

        const double output_load_pf = arc_context.sta_arc->get_snk()->getLoad(
            AnalysisMode::kMax, trans_type);
        const double output_load =
            convertLoadToLibUnit(power_term.power_arc, output_load_pf);
        const double internal_power_value = internal_power_info->gatePower(
            trans_type, input_slew_ns.value_or(0.0), output_load);
        return power_term.power_arc->get_owner_cell()->convertTablePowerToMw(
            internal_power_value);
      };

      const double rise_power_mw = query_power(TransType::kRise);
      const double fall_power_mw = query_power(TransType::kFall);
      double arc_power =
          output_toggle * CalcAveragePower(rise_power_mw, fall_power_mw);

      if (power_term.when_expr != nullptr) {
        arc_power *= evalPreparedWhenExpr(power_term.when_expr.get());
      }

      pin_internal_power += arc_power;
      arc_context.inst_arc->set_internal_power(arc_power);
    }
  }

  return pin_internal_power;
}

auto calcPreparedClockPinPower(const PreparedPinContext& pin_context,
                               double output_pin_toggle) -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared clock-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared clock-input STA vertex is null.";

  for (const auto& power_term : pin_context.pin_power_terms) {
    auto rise_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kRise);
    if (!rise_slew) {
      LOG_ERROR_IF(!rise_slew)
          << pin_context.sta_vertex->getName() << " rise slew is not exist.";
      rise_slew = 0.0;
    }

    const double rise_power = power_term.internal_power->gatePower(
        TransType::kRise, rise_slew.value_or(0.0), std::nullopt);
    const double rise_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(rise_power);

    auto fall_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kFall);
    if (!fall_slew) {
      LOG_ERROR_IF(!fall_slew)
          << pin_context.sta_vertex->getName() << " fall slew is not exist.";
      fall_slew = 0.0;
    }

    const double fall_power = power_term.internal_power->gatePower(
        TransType::kFall, fall_slew.value_or(0.0), std::nullopt);
    const double fall_power_mw =
        pin_context.lib_cell->convertTablePowerToMw(fall_power);
    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);
    const double clock_pin_toggle = getPreparedToggle(pin_context.pwr_vertex);

    const double output_flip_power =
        HalfToggle(clock_pin_toggle) *
        (pin_context.sta_vertex->isRisingTriggered() ? rise_power_mw
                                                     : fall_power_mw);
    const double output_no_flip_power =
        (clock_pin_toggle - output_pin_toggle) * average_power_mw +
        HalfToggle(clock_pin_toggle) *
            (pin_context.sta_vertex->isRisingTriggered() ? rise_power_mw
                                                         : fall_power_mw);

    if (power_term.when_expr != nullptr) {
      pin_internal_power += evalPreparedWhenExpr(power_term.when_expr.get()) *
                            (output_flip_power + output_no_flip_power);
    } else {
      pin_internal_power += output_flip_power + output_no_flip_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedSeqInputPinPower(const PreparedPinContext& pin_context)
    -> double {
  double pin_internal_power = 0.0;
  LOG_FATAL_IF(pin_context.lib_cell == nullptr)
      << "prepared seq-input liberty cell is null.";
  LOG_FATAL_IF(pin_context.sta_vertex == nullptr)
      << "prepared seq-input STA vertex is null.";

  for (const auto& power_term : pin_context.pin_power_terms) {
    if (pin_context.sta_vertex->getSlewBucket().empty()) {
      continue;
    }

    auto rise_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kRise);
    double rise_power_mw = 0.0;
    LOG_ERROR_IF(!rise_slew)
        << pin_context.sta_vertex->getName() << " rise slew is not exist.";
    if (rise_slew) {
      const double rise_power = power_term.internal_power->gatePower(
          TransType::kRise, rise_slew.value_or(0.0), std::nullopt);
      rise_power_mw = pin_context.lib_cell->convertTablePowerToMw(rise_power);
    }

    auto fall_slew =
        pin_context.sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kFall);
    LOG_ERROR_IF(!fall_slew)
        << pin_context.sta_vertex->getName() << " fall slew is not exist.";
    double fall_power_mw = rise_power_mw;
    if (fall_slew) {
      const double fall_power = power_term.internal_power->gatePower(
          TransType::kFall, fall_slew.value_or(0.0), std::nullopt);
      fall_power_mw = pin_context.lib_cell->convertTablePowerToMw(fall_power);
    }

    const double average_power_mw =
        CalcAveragePower(rise_power_mw, fall_power_mw);
    const double input_pin_toggle = getPreparedToggle(pin_context.pwr_vertex);
    const double internal_power = input_pin_toggle * average_power_mw;

    if (power_term.when_expr != nullptr) {
      pin_internal_power +=
          evalPreparedWhenExpr(power_term.when_expr.get()) * internal_power;
    } else {
      pin_internal_power += internal_power;
    }
  }

  return pin_internal_power;
}

auto calcPreparedCombInternalPower(const PreparedCellContext& cell_context)
    -> double {
  double inst_internal_power = 0.0;
  double input_sum_toggle = 0.0;
  double output_pin_toggle = 0.0;

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pin->isInput()) {
      input_sum_toggle += getPreparedToggle(pin_context.pwr_vertex);
    } else {
      output_pin_toggle = getPreparedToggle(pin_context.pwr_vertex);
    }
  }

  for (const auto& pin_context : cell_context.pins) {
    double pin_internal_power = 0.0;
    if (pin_context.pin->isInput()) {
      pin_internal_power = calcPreparedCombInputPinPower(
          pin_context, input_sum_toggle, output_pin_toggle);
      pin_context.pwr_vertex->set_internal_power(pin_internal_power);
      inst_internal_power += pin_internal_power;
    }

    if (pin_context.pin->isOutput()) {
      if (pin_context.pin->get_net()) {
        pin_internal_power = calcPreparedOutputPinPower(pin_context);
        inst_internal_power += pin_internal_power;
      }
    }
  }

  return inst_internal_power;
}

auto calcPreparedSeqInternalPower(const PreparedCellContext& cell_context)
    -> double {
  double inst_internal_power = 0.0;
  double output_pin_toggle = 0.0;

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pin->isOutput()) {
      output_pin_toggle = getPreparedToggle(pin_context.pwr_vertex);
    }
  }

  for (const auto& pin_context : cell_context.pins) {
    if (pin_context.pwr_vertex->is_const()) {
      continue;
    }

    if (pin_context.pin->isInput()) {
      double pin_internal_power = 0.0;
      if (pin_context.sta_vertex->is_clock()) {
        pin_internal_power =
            calcPreparedClockPinPower(pin_context, output_pin_toggle);
      } else {
        pin_internal_power = calcPreparedSeqInputPinPower(pin_context);
      }

      inst_internal_power += pin_internal_power;
      pin_context.pwr_vertex->set_internal_power(pin_internal_power);
    } else {
      inst_internal_power += calcPreparedOutputPinPower(pin_context);
    }
  }

  return inst_internal_power;
}

auto calcPreparedInternalPowerLegacy(
    InternalPowerContext& prepared_context,
    std::vector<std::unique_ptr<PwrInternalData>>& internal_powers) -> double {
  internal_powers.clear();
  double total_internal_power_mw = 0.0;
  for (const auto& cell_context : prepared_context.cells) {
    auto* design_inst = cell_context.inst;
    auto* inst_cell = design_inst->get_inst_cell();
    double inst_internal_power_mw = 0.0;

    if (inst_cell->isMacroCell()) {
      // TODO
    } else if (cell_context.is_seq) {
      inst_internal_power_mw = calcPreparedSeqInternalPower(cell_context);
    } else {
      inst_internal_power_mw = calcPreparedCombInternalPower(cell_context);
    }

    const double nom_voltage = inst_cell->get_owner_lib()->get_nom_voltage();
    auto internal_data = std::make_unique<PwrInternalData>(
        design_inst, MW_TO_W(inst_internal_power_mw));
    internal_data->set_nom_voltage(nom_voltage);
    internal_powers.emplace_back(std::move(internal_data));

    total_internal_power_mw += inst_internal_power_mw;
  }
  return total_internal_power_mw;
}

auto calcPreparedInternalPowerLegacyTotalOnly(
    InternalPowerContext& prepared_context) -> double {
  double total_internal_power_mw = 0.0;
  for (const auto& cell_context : prepared_context.cells) {
    auto* inst_cell = cell_context.inst->get_inst_cell();
    double inst_internal_power_mw = 0.0;

    if (inst_cell->isMacroCell()) {
      // TODO
    } else if (cell_context.is_seq) {
      inst_internal_power_mw = calcPreparedSeqInternalPower(cell_context);
    } else {
      inst_internal_power_mw = calcPreparedCombInternalPower(cell_context);
    }

    total_internal_power_mw += inst_internal_power_mw;
  }
  return total_internal_power_mw;
}

}  // namespace

Power* Power::_power = nullptr;

/**
 * @brief Get the top power instance, if not, create one.
 *
 * @return Power*
 */
Power* Power::getOrCreatePower(StaGraph* sta_graph) {
  static std::mutex mt;
  if (_power == nullptr) {
    if (_power == nullptr) {
      std::lock_guard<std::mutex> lock(mt);
      _power = new Power(sta_graph);
    }
  }
  return _power;
}

/**
 * @brief Destroy the power.
 *
 */
void Power::destroyPower() {
  delete _power;
  _power = nullptr;
}

/**
 * @brief build power graph.
 *
 * @return unsigned
 */
unsigned Power::buildGraph() {
  _icts_char_support.internal_power_context.reset();
  PwrBuildGraph build_graph(_power_graph);
  build_graph(_power_graph.get_sta_graph());
  _power_graph.set_pwr_seq_graph(&_power_seq_graph);
  return 1;
}

/**
 * @brief setup power relative clock.
 *
 * @param fastest_clock
 * @param sta_clocks
 * @return unsigned
 */
unsigned Power::setupClock(PwrClock&& fastest_clock,
                           Vector<StaClock*>&& sta_clocks) {
  _power_graph.set_fastest_clock(std::move(fastest_clock));
  _power_graph.set_sta_clocks(std::move(sta_clocks));
  return 1;
}

/**
 * @brief read a VCD file by rust vcd parser
 *
 * @param vcd_path
 * @param top_instance_name
 * @return unsigned
 */
unsigned Power::readRustVCD(const char* vcd_path,
                            const char* top_instance_name) {
  LOG_INFO << "read vcd start";
  _rust_vcd_wrapper.readVcdFile(vcd_path);
  _rust_vcd_wrapper.buildAnnotateDB(top_instance_name);
  _rust_vcd_wrapper.calcScopeToggleAndSp(top_instance_name);
  LOG_INFO << "read vcd end";

  return 1;
}

/**
 * @brief annotate vcd toggle sp to pwr vertex.
 *
 * @param annotate_db
 * @return unsigned
 */
unsigned Power::annotateToggleSP() {
  LOG_INFO << "annotate toggle sp start";

  AnnotateToggleSP annotate_toggle_SP;
  annotate_toggle_SP.set_annotate_db(_rust_vcd_wrapper.get_annotate_db());

  unsigned is_ok = annotate_toggle_SP(&_power_graph);
  LOG_INFO << "annotate toggle sp end";

  return is_ok;
}

/**
 * @brief build sequential graph.
 *
 * @return unsigned
 */
unsigned Power::buildSeqGraph() {
  PwrBuildSeqGraph build_seq_graph;
  build_seq_graph(&_power_graph);
  _power_seq_graph = std::move(build_seq_graph.takePwrSeqGraph());
  return 1;
}

/**
 * @brief dump sequential graph in graphviz format.
 *
 * @return unsigned
 */
unsigned Power::dumpSeqGraphViz() {
  PwrDumpSeqGraphViz dump_seq_graph_viz;
  return dump_seq_graph_viz(&_power_seq_graph);
}

/**
 * @brief check pipline loop for break loop.
 *
 * @return unsigned
 */
unsigned Power::checkPipelineLoop() {
  PwrCheckPipelineLoop check_pipeline_loop;
  return check_pipeline_loop(&_power_seq_graph);
}

/**
 * @brief levelize the sequential graph.
 *
 * @return unsigned
 */
unsigned Power::levelizeSeqGraph() {
  PwrLevelizeSeq levelize_seq;
  return levelize_seq(&_power_seq_graph);
}

/**
 * @brief dump the power graph.
 *
 * @return unsigned
 */
unsigned Power::dumpGraph() {
  PwrDumpGraphYaml dump_graph;
  return dump_graph(&_power_graph);
}

/**
 * @brief Propagate clock vertexes.
 *
 * @param sta_clocks
 * @return unsigned
 */
unsigned Power::propagateClock() {
  PwrPropagateClock propagate_clock;
  return propagate_clock(&_power_graph);
}

/**
 * @brief propagate const to set const node.
 *
 * @return unsigned
 */
unsigned Power::propagateConst() {
  PwrPropagateConst propagate_const;
  return propagate_const(&_power_graph);
}

/**
 * @brief propagate toggle and sp.
 *
 * @return unsigned
 */
unsigned Power::propagateToggleSP() {
  PwrPropagateToggleSP propagate_toggle_sp;
  return propagate_toggle_sp(&_power_graph);
}

void Power::resetCalcPowerData() {
  resetCalcLeakagePowerData();
  resetCalcInternalPowerData();
  resetCalcSwitchPowerData();
}

void Power::resetCalcLeakagePowerData() {
  _leakage_powers.clear();
  resetLeakageGroupData(_obj_to_datas);
}

void Power::resetCalcInternalPowerData() {
  _internal_powers.clear();
  resetGraphInternalPower(_power_graph);
  resetInternalGroupData(_obj_to_datas);
}

void Power::resetCalcSwitchPowerData() {
  _switch_powers.clear();
  resetSwitchGroupData(_obj_to_datas);
}

auto icts_char::PowerFacade::prepareCharInternalPowerContext() -> unsigned {
  return _power.prepareInternalPowerContext();
}

auto icts_char::PowerFacade::prepareCharClockPowerData() -> unsigned {
  return ::ipower::prepareCharClockPowerData(_power);
}

auto icts_char::PowerFacade::prepareCharInternalPowerSampleContext() -> unsigned {
  return _power.prepareInternalPowerSampleContext();
}

auto icts_char::PowerFacade::freezeCharInternalPowerContext() -> unsigned {
  return _power.freezePreparedInternalPowerSamplePowerContext();
}

auto icts_char::PowerFacade::resetCharLeakagePowerData() -> void {
  _power.resetCalcLeakagePowerData();
}

auto icts_char::PowerFacade::refreshCharInternalPowerLoadContext() -> void {
  _power.refreshPreparedInternalPowerLoadSampleContext();
}

auto icts_char::PowerFacade::calcFrozenCharInternalPowerTotal(
    double& total_internal_power_w) -> unsigned {
  return _power.calcPreparedInternalPowerTotalOnlyFrozenPower(
      total_internal_power_w);
}

unsigned Power::prepareInternalPowerContext() {
  auto prepared_context = std::make_shared<icts_char::InternalPowerContext>();
  auto& power_graph = get_power_graph();
  auto* sta_graph = power_graph.get_sta_graph();
  LOG_FATAL_IF(sta_graph == nullptr)
      << "power graph sta graph is null when preparing internal power context.";

  std::unordered_set<PwrVertex*> touched_vertices;
  std::unordered_set<PwrInstArc*> touched_inst_arcs;

  PwrCell* cell = nullptr;
  FOREACH_PWR_CELL((&power_graph), cell) {
    auto* design_inst = cell->get_design_inst();
    auto* inst_cell = design_inst->get_inst_cell();
    LOG_FATAL_IF(inst_cell == nullptr)
        << "instance " << design_inst->get_name() << " has no liberty cell.";

    PreparedCellContext cell_context;
    cell_context.inst = design_inst;
    cell_context.is_seq = inst_cell->isSequentialCell() != 0U;

    std::unordered_map<std::string, PwrVertex*> pin_name_to_vertex;
    Pin* pin = nullptr;
    FOREACH_INSTANCE_PIN(design_inst, pin) {
      auto sta_vertex = sta_graph->findVertex(pin);
      LOG_FATAL_IF(!sta_vertex.has_value())
          << "not found sta vertex for pin " << pin->getFullName();
      auto* pwr_vertex = power_graph.staToPwrVertex(*sta_vertex);
      LOG_FATAL_IF(pwr_vertex == nullptr)
          << "not found power vertex for pin " << pin->getFullName();
      pin_name_to_vertex.emplace(pin->get_name(), pwr_vertex);
    }

    FOREACH_INSTANCE_PIN(design_inst, pin) {
      auto sta_vertex = sta_graph->findVertex(pin);
      LOG_FATAL_IF(!sta_vertex.has_value())
          << "not found sta vertex for pin " << pin->getFullName();
      auto* pwr_vertex = power_graph.staToPwrVertex(*sta_vertex);
      LOG_FATAL_IF(pwr_vertex == nullptr)
          << "not found power vertex for pin " << pin->getFullName();

      PreparedPinContext pin_context;
      pin_context.pin = pin;
      pin_context.cell_port = pin->get_cell_port();
      pin_context.lib_cell = pin_context.cell_port
                                 ? pin_context.cell_port->get_ower_cell()
                                 : nullptr;
      pin_context.sta_vertex = *sta_vertex;
      pin_context.pwr_vertex = pwr_vertex;
      pin_context.output_sta_vertex = *sta_vertex;
      pin_context.output_pwr_vertex = pwr_vertex;
      touched_vertices.insert(pwr_vertex);

      if (pin->isInput()) {
        LOG_FATAL_IF(pin_context.cell_port == nullptr)
            << "input pin " << pin->getFullName()
            << " has no liberty cell port.";
        LibInternalPowerInfo* internal_power = nullptr;
        FOREACH_INTERNAL_POWER(pin_context.cell_port, internal_power) {
          PreparedPinPowerTerm power_term;
          power_term.internal_power = internal_power;
          power_term.when_expr =
              prepareWhenExpr(internal_power->get_when(), pin_name_to_vertex);
          pin_context.pin_power_terms.emplace_back(std::move(power_term));
        }
      }

      if (pin->isOutput()) {
        if (pin->isInout()) {
          pin_context.output_sta_vertex =
              sta_graph->getAssistant(pin_context.output_sta_vertex);
        }
        LOG_FATAL_IF(pin_context.output_sta_vertex == nullptr)
            << "not found output sta vertex for pin " << pin->getFullName();
        pin_context.output_pwr_vertex =
            power_graph.staToPwrVertex(pin_context.output_sta_vertex);
        LOG_FATAL_IF(pin_context.output_pwr_vertex == nullptr)
            << "not found output power vertex for pin " << pin->getFullName();
        touched_vertices.insert(pin_context.output_pwr_vertex);

        FOREACH_SNK_PWR_ARC(pin_context.output_pwr_vertex, snk_arc) {
          if (snk_arc->isNetArc()) {
            continue;
          }

          auto* inst_arc = dynamic_cast<PwrInstArc*>(snk_arc);
          if (inst_arc == nullptr) {
            continue;
          }

          auto* power_arc_set = inst_arc->get_power_arc_set();
          if (power_arc_set == nullptr) {
            continue;
          }

          auto* src_input_pwr_vertex = snk_arc->get_src();
          auto* src_input_sta_vertex =
              power_graph.pwrToStaVertex(src_input_pwr_vertex);
          LOG_FATAL_IF(src_input_sta_vertex == nullptr)
              << "not found source sta vertex for prepared power arc.";

          auto src_arcs =
              pin_context.output_sta_vertex->getSrcArc(src_input_sta_vertex);
          LOG_FATAL_IF(src_arcs.empty())
              << "not found source STA arc for pin " << pin->getFullName();

          PreparedOutputArcContext arc_context;
          arc_context.inst_arc = inst_arc;
          arc_context.sta_arc = src_arcs.front();
          touched_inst_arcs.insert(inst_arc);

          LibPowerArc* power_arc = nullptr;
          FOREACH_POWER_LIB_ARC(power_arc_set, power_arc) {
            PreparedOutputArcTerm power_term;
            power_term.power_arc = power_arc;
            power_term.when_expr =
                prepareWhenExpr(power_arc->get_when(), pin_name_to_vertex);
            arc_context.terms.emplace_back(std::move(power_term));
          }

          pin_context.output_arc_contexts.emplace_back(std::move(arc_context));
        }
      }

      cell_context.pins.emplace_back(std::move(pin_context));
    }

    prepared_context->cells.emplace_back(std::move(cell_context));
  }

  prepared_context->touched_vertices.assign(touched_vertices.begin(),
                                            touched_vertices.end());
  prepared_context->touched_inst_arcs.assign(touched_inst_arcs.begin(),
                                             touched_inst_arcs.end());
  _icts_char_support.internal_power_context = std::move(prepared_context);
  return 1;
}

unsigned Power::prepareInternalPowerSampleContext() {
  if (_icts_char_support.internal_power_context == nullptr) {
    return 0;
  }

  auto& prepared_context = *_icts_char_support.internal_power_context;
  prepared_context.sample_pwr_vertices.clear();
  prepared_context.sample_sta_vertices.clear();
  prepared_context.sample_when_expr_count = 0;
  prepared_context.has_prepared_sample_context = false;
  prepared_context.has_frozen_power_sample_context = false;

  std::unordered_map<PwrVertex*, int> pwr_vertex_to_index;
  std::unordered_map<StaVertex*, int> sta_vertex_to_index;

  for (auto& cell_context : prepared_context.cells) {
    for (auto& pin_context : cell_context.pins) {
      pin_context.pwr_vertex_sample_index =
          registerSamplePwrVertex(pin_context.pwr_vertex, pwr_vertex_to_index,
                                  prepared_context.sample_pwr_vertices);
      pin_context.sta_vertex_sample_index =
          registerSampleStaVertex(pin_context.sta_vertex, sta_vertex_to_index,
                                  prepared_context.sample_sta_vertices);

      for (auto& power_term : pin_context.pin_power_terms) {
        indexPreparedWhenExpr(power_term.when_expr.get(), pwr_vertex_to_index,
                              prepared_context.sample_pwr_vertices,
                              prepared_context.sample_when_expr_count);
      }

      for (auto& arc_context : pin_context.output_arc_contexts) {
        LOG_FATAL_IF(arc_context.sta_arc == nullptr)
            << "prepared output STA arc is null.";
        arc_context.src_sta_vertex_sample_index = registerSampleStaVertex(
            arc_context.sta_arc->get_src(), sta_vertex_to_index,
            prepared_context.sample_sta_vertices);
        arc_context.snk_sta_vertex_sample_index = registerSampleStaVertex(
            arc_context.sta_arc->get_snk(), sta_vertex_to_index,
            prepared_context.sample_sta_vertices);
        arc_context.is_positive_arc = arc_context.sta_arc->isPositiveArc();

        for (auto& power_term : arc_context.terms) {
          indexPreparedWhenExpr(power_term.when_expr.get(), pwr_vertex_to_index,
                                prepared_context.sample_pwr_vertices,
                                prepared_context.sample_when_expr_count);
        }
      }
    }
  }

  prepared_context.sample_toggle_values.assign(
      prepared_context.sample_pwr_vertices.size(), 0.0);
  prepared_context.sample_sp_values.assign(
      prepared_context.sample_pwr_vertices.size(), 0.0);
  prepared_context.sample_rise_slew_values.assign(
      prepared_context.sample_sta_vertices.size(), 0.0);
  prepared_context.sample_fall_slew_values.assign(
      prepared_context.sample_sta_vertices.size(), 0.0);
  prepared_context.sample_has_rise_slew.assign(
      prepared_context.sample_sta_vertices.size(), 0);
  prepared_context.sample_has_fall_slew.assign(
      prepared_context.sample_sta_vertices.size(), 0);
  prepared_context.sample_has_slew_bucket.assign(
      prepared_context.sample_sta_vertices.size(), 0);
  prepared_context.sample_rise_load_values.assign(
      prepared_context.sample_sta_vertices.size(), 0.0);
  prepared_context.sample_fall_load_values.assign(
      prepared_context.sample_sta_vertices.size(), 0.0);
  prepared_context.sample_when_values.assign(
      prepared_context.sample_when_expr_count, 0.0);
  prepared_context.sample_when_ready.assign(
      prepared_context.sample_when_expr_count, 0);
  prepared_context.has_prepared_sample_context = true;
  return 1;
}

void Power::refreshPreparedInternalPowerSampleContext() {
  if (_icts_char_support.internal_power_context == nullptr ||
      !_icts_char_support.internal_power_context->has_prepared_sample_context) {
    return;
  }
  refreshPreparedInternalPowerSampleSnapshot(
      *_icts_char_support.internal_power_context);
}

unsigned Power::freezePreparedInternalPowerSamplePowerContext() {
  if (_icts_char_support.internal_power_context == nullptr) {
    return 0;
  }

  auto& prepared_context = *_icts_char_support.internal_power_context;
  if (!prepared_context.has_prepared_sample_context &&
      prepareInternalPowerSampleContext() == 0U) {
    return 0;
  }

  refreshPreparedInternalPowerPowerSampleSnapshot(prepared_context);
  std::fill(prepared_context.sample_when_ready.begin(),
            prepared_context.sample_when_ready.end(), 0);
  primePreparedWhenExprSampleCache(prepared_context);
  prepared_context.has_frozen_power_sample_context = true;
  return 1;
}

void Power::refreshPreparedInternalPowerLoadSampleContext() {
  if (_icts_char_support.internal_power_context == nullptr ||
      !_icts_char_support.internal_power_context->has_prepared_sample_context) {
    return;
  }

  auto& prepared_context = *_icts_char_support.internal_power_context;
  const auto sta_vertex_num = prepared_context.sample_sta_vertices.size();
  for (size_t idx = 0; idx < sta_vertex_num; ++idx) {
    auto* sta_vertex = prepared_context.sample_sta_vertices[idx];
    LOG_FATAL_IF(sta_vertex == nullptr)
        << "prepared sample STA vertex is null.";
    prepared_context.sample_rise_load_values[idx] =
        sta_vertex->getLoad(AnalysisMode::kMax, TransType::kRise);
    prepared_context.sample_fall_load_values[idx] =
        sta_vertex->getLoad(AnalysisMode::kMax, TransType::kFall);
  }
}

void Power::refreshPreparedInternalPowerSlewSampleContext() {
  if (_icts_char_support.internal_power_context == nullptr ||
      !_icts_char_support.internal_power_context->has_prepared_sample_context) {
    return;
  }

  auto& prepared_context = *_icts_char_support.internal_power_context;
  const auto sta_vertex_num = prepared_context.sample_sta_vertices.size();
  for (size_t idx = 0; idx < sta_vertex_num; ++idx) {
    auto* sta_vertex = prepared_context.sample_sta_vertices[idx];
    LOG_FATAL_IF(sta_vertex == nullptr)
        << "prepared sample STA vertex is null.";

    auto rise_slew =
        sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kRise);
    auto fall_slew =
        sta_vertex->getSlewNs(AnalysisMode::kMax, TransType::kFall);
    prepared_context.sample_has_rise_slew[idx] = rise_slew.has_value();
    prepared_context.sample_has_fall_slew[idx] = fall_slew.has_value();
    prepared_context.sample_rise_slew_values[idx] = rise_slew.value_or(0.0);
    prepared_context.sample_fall_slew_values[idx] = fall_slew.value_or(0.0);
    prepared_context.sample_has_slew_bucket[idx] =
        sta_vertex->getSlewBucket().empty() ? 0U : 1U;
  }
}

void Power::resetPreparedInternalPowerData() {
  if (_icts_char_support.internal_power_context == nullptr) {
    resetCalcInternalPowerData();
    return;
  }

  _internal_powers.clear();
  for (auto* pwr_vertex : _icts_char_support.internal_power_context
                              ->touched_vertices) {
    if (pwr_vertex != nullptr) {
      pwr_vertex->set_internal_power(0.0);
    }
  }
  for (auto* inst_arc : _icts_char_support.internal_power_context
                             ->touched_inst_arcs) {
    if (inst_arc != nullptr) {
      inst_arc->set_internal_power(0.0);
    }
  }
  resetInternalGroupData(_obj_to_datas);
}

unsigned Power::calcPreparedInternalPower() {
  if (_icts_char_support.internal_power_context == nullptr) {
    return calcInternalPower();
  }

  (void)calcPreparedInternalPowerLegacy(
      *_icts_char_support.internal_power_context, _internal_powers);
  return 1;
}

unsigned Power::calcPreparedInternalPowerBySampleContext() {
  if (_icts_char_support.internal_power_context == nullptr) {
    return calcInternalPower();
  }

  if (!_icts_char_support.internal_power_context->has_prepared_sample_context) {
    if (prepareInternalPowerSampleContext() == 0U) {
      (void)calcPreparedInternalPowerLegacy(
          *_icts_char_support.internal_power_context, _internal_powers);
      return 1;
    }
  }

  refreshPreparedInternalPowerSampleContext();
  _internal_powers.clear();

  std::vector<double> per_cell_internal_power_mw;
  const double internal_power_result = calcPreparedInternalPowerTotalBySample(
      *_icts_char_support.internal_power_context, true,
      &per_cell_internal_power_mw);
  LOG_FATAL_IF(per_cell_internal_power_mw.size() !=
               _icts_char_support.internal_power_context->cells.size())
      << "prepared sample internal-power vector size mismatch.";

  // Keep the legacy per-cell output contract for existing callers.
  for (size_t idx = 0;
       idx < _icts_char_support.internal_power_context->cells.size();
       ++idx) {
    const auto& cell_context =
        _icts_char_support.internal_power_context->cells[idx];
    auto* design_inst = cell_context.inst;
    auto* inst_cell = design_inst->get_inst_cell();
    const double cell_internal_power_mw = per_cell_internal_power_mw[idx];

    const double nom_voltage = inst_cell->get_owner_lib()->get_nom_voltage();
    auto internal_data = std::make_unique<PwrInternalData>(
        design_inst, MW_TO_W(cell_internal_power_mw));
    internal_data->set_nom_voltage(nom_voltage);
    _internal_powers.emplace_back(std::move(internal_data));
  }

  (void)internal_power_result;
  return 1;
}

unsigned Power::calcPreparedInternalPowerTotalOnly(
    double& total_internal_power_w) {
  total_internal_power_w = 0.0;
  if (_icts_char_support.internal_power_context == nullptr) {
    if (calcInternalPower() == 0U) {
      return 0;
    }
    total_internal_power_w = getSumInternalPower();
    return 1;
  }

  if (!_icts_char_support.internal_power_context->has_prepared_sample_context) {
    if (prepareInternalPowerSampleContext() == 0U) {
      total_internal_power_w = MW_TO_W(calcPreparedInternalPowerLegacyTotalOnly(
          *_icts_char_support.internal_power_context));
      return 1;
    }
  }

  refreshPreparedInternalPowerSampleContext();
  const double internal_power_mw = calcPreparedInternalPowerTotalBySample(
      *_icts_char_support.internal_power_context, false);
  total_internal_power_w = MW_TO_W(internal_power_mw);
  return 1;
}

unsigned Power::calcPreparedInternalPowerTotalOnlyFrozenPower(
    double& total_internal_power_w) {
  total_internal_power_w = 0.0;
  if (_icts_char_support.internal_power_context == nullptr) {
    return calcPreparedInternalPowerTotalOnly(total_internal_power_w);
  }

  auto& prepared_context = *_icts_char_support.internal_power_context;
  if (!prepared_context.has_prepared_sample_context &&
      prepareInternalPowerSampleContext() == 0U) {
    total_internal_power_w =
        MW_TO_W(calcPreparedInternalPowerLegacyTotalOnly(prepared_context));
    return 1;
  }

  if (!prepared_context.has_frozen_power_sample_context &&
      freezePreparedInternalPowerSamplePowerContext() == 0U) {
    return calcPreparedInternalPowerTotalOnly(total_internal_power_w);
  }

  refreshPreparedInternalPowerSlewSampleContext();
  const double internal_power_mw =
      calcPreparedInternalPowerTotalBySample(prepared_context, false);
  total_internal_power_w = MW_TO_W(internal_power_mw);
  return 1;
}

/**
 * @brief calc leakage power.
 *
 * @return unsigned
 */
unsigned Power::calcLeakagePower() {
  PwrCalcLeakagePower calc_leakage_power;
  calc_leakage_power(&_power_graph);
  _leakage_powers = std::move(calc_leakage_power.takeLeakagePowers());
  return 1;
}

/**
 * @brief calc internal power.
 *
 * @return unsigned
 */
unsigned Power::calcInternalPower() {
  PwrCalcInternalPower calc_internal_power;
  calc_internal_power(&_power_graph);
  _internal_powers = std::move(calc_internal_power.takeInternalPowers());
  return 1;
}

/**
 * @brief  calc switch power.
 *
 * @return unsigned
 */
unsigned Power::calcSwitchPower() {
  PwrCalcSwitchPower calc_switch_power;
  calc_switch_power(&_power_graph);
  _switch_powers = std::move(calc_switch_power.takeSwitchPowers());
  return 1;
}

/**
 * @brief the wrapper for levelization seq graph, const propagation, toggle/sp
 * propagation, analyze power.
 *
 * @return unsigned
 */
unsigned Power::updatePower() {
  {
    ieda::Stats stats;
    LOG_INFO << "power calculation start";

    // thirdly analyze power.
    calcLeakagePower();
    calcInternalPower();
    calcSwitchPower();
    analyzeGroupPower();

    LOG_INFO << "power calculation end";
    double memory_delta = stats.memoryDelta();
    LOG_INFO << "power calculation memory usage " << memory_delta << "MB";
    double time_delta = stats.elapsedRunTime();
    LOG_INFO << "power calculation time elapsed " << time_delta << "s";
  }

  return 1;
}

/**
 * @brief get the instance owned group.
 *
 * @param inst
 * @return std::optional<PwrGroupData::PwrGroupType>
 */
std::optional<PwrGroupData::PwrGroupType> Power::getInstPowerGroup(
    Instance* the_inst) {
  auto* lib_cell = the_inst->get_inst_cell();
  std::array<std::function<std::optional<PwrGroupData::PwrGroupType>(Instance *
                                                                     the_inst)>,
             7>
      group_prioriy_array{
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether io cell.
            return std::nullopt;
          },
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether memory.
            return std::nullopt;
          },
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether black box.
            return std::nullopt;
          },
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether register.
            if (lib_cell->isSequentialCell()) {
              return PwrGroupData::PwrGroupType::kSeq;
            }
            return std::nullopt;
          },
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether clock network.
            Pin* pin;
            FOREACH_INSTANCE_PIN(the_inst, pin) {
              auto* the_pwr_vertex = _power_graph.getPowerVertex(pin);
              if (the_pwr_vertex->is_clock_network()) {
                return PwrGroupData::PwrGroupType::kClockNetwork;
              }
            }
            return std::nullopt;
          },
          [this, lib_cell](
              Instance* the_inst) -> std::optional<PwrGroupData::PwrGroupType> {
            // judge whether register.
            if (!lib_cell->isSequentialCell()) {
              return PwrGroupData::PwrGroupType::kComb;
            }
            return std::nullopt;
          }};

  for (auto group_type_func : group_prioriy_array) {
    auto power_type = group_type_func(the_inst);
    if (power_type) {
      return power_type;
    }
  }
  return std::nullopt;
}

/**
 * @brief analyze power by group.
 *
 * @return unsigned
 */
unsigned Power::analyzeGroupPower() {
  auto add_group_data_from_analysis_data = [this](auto group_type,
                                                  DesignObject* design_obj,
                                                  PwrAnalysisData* power_data) {
    /*the lambda of set power data*/
    auto set_power_data = [this, &power_data](PwrGroupData* group_data) {
      double power_data_value = power_data->getPowerDataValue();
      if (power_data->isLeakageData()) {
        group_data->set_leakage_power(power_data_value);
      } else if (power_data->isInternalData()) {
        group_data->set_internal_power(power_data_value);
      } else {
        group_data->set_switch_power(power_data_value);
      }
      group_data->set_nom_voltage(power_data->get_nom_voltage());
    };

    // find the design object of power data
    auto this_data = _obj_to_datas.find(design_obj);
    if (this_data != _obj_to_datas.end()) {
      set_power_data(this_data->second.get());
    } else {
      auto group_data = std::make_unique<PwrGroupData>(group_type, design_obj);
      set_power_data(group_data.get());
      addGroupData(std::move(group_data));
    }
  };

  PwrLeakageData* leakage_power_data;
  FOREACH_PWR_LEAKAGE_POWER(this, leakage_power_data) {
    auto* inst = dynamic_cast<Instance*>(leakage_power_data->get_design_obj());
    LOG_FATAL_IF(!inst) << "leakage power instance is not exist.";
    auto group_type = getInstPowerGroup(inst);
    if (group_type) {
      add_group_data_from_analysis_data(group_type.value(), inst,
                                        leakage_power_data);
    } else {
      LOG_INFO << "can not find group type for" << inst->get_name();
    }
  }

  PwrInternalData* internal_power_data;
  FOREACH_PWR_INTERNAL_POWER(this, internal_power_data) {
    auto* inst = dynamic_cast<Instance*>(internal_power_data->get_design_obj());
    LOG_FATAL_IF(!inst) << "internal power instance is not exist.";
    auto group_type = getInstPowerGroup(inst);
    if (group_type) {
      add_group_data_from_analysis_data(group_type.value(), inst,
                                        internal_power_data);
    } else {
      LOG_INFO << "can not find group type for" << inst->get_name();
    }
  }

  PwrSwitchData* switch_power_data;
  FOREACH_PWR_SWITCH_POWER(this, switch_power_data) {
    auto* net = dynamic_cast<Net*>(switch_power_data->get_design_obj());
    auto* driver_obj = net->getDriver();

    auto* the_sta_graph = _power_graph.get_sta_graph();
    auto driver_sta_vertex = the_sta_graph->findVertex(driver_obj);

    PwrVertex* driver_pwr_vertex = nullptr;
    if (driver_sta_vertex) {
      driver_pwr_vertex = _power_graph.staToPwrVertex(*driver_sta_vertex);
    } else {
      LOG_FATAL << "not found driver sta vertex.";
    }

    // TODO  input port
    if (driver_pwr_vertex->is_input_port()) {
      continue;
    }

    auto* driver_inst = driver_pwr_vertex->getOwnInstance();
    if (!driver_inst) {
      LOG_FATAL << "not found driver instance.";
    }

    auto group_type = getInstPowerGroup(driver_inst);
    if (group_type) {
      add_group_data_from_analysis_data(group_type.value(), driver_inst,
                                        switch_power_data);
    } else {
      LOG_INFO << "can not find group type for" << driver_inst->get_name();
    }
  }
  return 1;
}

/**
 * @brief report power
 *
 * @param rpt_file_name
 * @return unsigned
 */
unsigned Power::reportSummaryPower(const char* rpt_file_name,
                                   PwrAnalysisMode pwr_analysis_mode) {
  PwrReportPowerSummary report_power(rpt_file_name, pwr_analysis_mode);
  report_power(this);
  auto& report_summary_data = report_power.get_report_summary_data();
  auto report_tbl = report_power.createReportTable("Power Analysis Report");

  std::map<PwrGroupData::PwrGroupType, std::string> group_type_to_string = {
      {PwrGroupData::PwrGroupType::kIOPad, "io_pad"},
      {PwrGroupData::PwrGroupType::kMemory, "memory"},
      {PwrGroupData::PwrGroupType::kBlackBox, "black_box"},
      {PwrGroupData::PwrGroupType::kClockNetwork, "clock_network"},
      {PwrGroupData::PwrGroupType::kRegister, "register"},
      {PwrGroupData::PwrGroupType::kComb, "combinational"},
      {PwrGroupData::PwrGroupType::kSeq, "sequential"}};

  // lambda for print power data float to string.
  auto data_str = [](double data) { return Str::printf("%.3e", data); };
  auto data_str_f = [](double data) { return Str::printf("%.3f", data); };

  double total_power = report_summary_data.get_total_power();
  /*Add group data to report table.*/
  PwrReportGroupSummaryData* report_group_data;
  FOREACH_REPORT_GROUP_DATA(&report_summary_data, report_group_data) {
    double group_total_power = report_group_data->get_total_power();
    // calc percentage
    double percentage = CalcPercentage(group_total_power / total_power);

    std::string str_percentage =
        std::string("(") + data_str_f(percentage) + std::string("%)");

    (*report_tbl) << group_type_to_string[report_group_data->get_group_type()]
                  << data_str(report_group_data->get_internal_power())
                  << data_str(report_group_data->get_switch_power())
                  << data_str(report_group_data->get_leakage_power())
                  << data_str(report_group_data->get_total_power())
                  << str_percentage << TABLE_ENDLINE;
  }

  LOG_INFO << "\n" << report_tbl->c_str();

  Time::stop();
  double elapsed_time = Time::elapsedTime();
  LOG_INFO << "iPA total elapsed time: " << elapsed_time << " seconds";

  auto close_file = [](std::FILE* fp) { std::fclose(fp); };

  std::unique_ptr<std::FILE, decltype(close_file)> f(
      std::fopen(rpt_file_name, "w"), close_file);

  std::fprintf(f.get(), "Generate the report at %s\n", Time::getNowWallTime());
  std::fprintf(f.get(), "iPA elapsed time: %.2f seconds.\n", elapsed_time);

  std::map<PwrAnalysisMode, std::string> analysis_mode_to_string = {
      {PwrAnalysisMode::kAveraged, "Averaged"},
      {PwrAnalysisMode::kTimeBase, "TimeBase"},
      {PwrAnalysisMode::kClockCycle, "ClockCycle"}};

  std::fprintf(f.get(), "Report : %s Power\n ",
               analysis_mode_to_string[pwr_analysis_mode].c_str());

  std::fprintf(f.get(), "%s", report_tbl->c_str());

  // print switch power
  double summary_switch_power = report_summary_data.get_net_switching_power();
  std::string summary_switch_power_percentage =
      std::string("(") +
      data_str_f(CalcPercentage(report_summary_data.get_net_switching_power() /
                                total_power)) +
      std::string("%)");
  std::fprintf(f.get(), "Net Switch Power   ==    %.3e %s\n",
               summary_switch_power, summary_switch_power_percentage.c_str());

  // print internal power
  double summary_internal_power = report_summary_data.get_cell_internal_power();
  std::string summary_internal_power_percentage =
      std::string("(") +
      data_str_f(CalcPercentage(report_summary_data.get_cell_internal_power() /
                                total_power)) +
      std::string("%)");
  std::fprintf(f.get(), "Cell Internal Power   ==    %.3e %s\n",
               summary_internal_power,
               summary_internal_power_percentage.c_str());

  // print leakage power
  double summary_leakage_power = report_summary_data.get_cell_leakage_power();
  std::string summary_leakage_power_percentage =
      std::string("(") +
      data_str_f(CalcPercentage(report_summary_data.get_cell_leakage_power() /
                                total_power)) +
      std::string("%)");
  std::fprintf(f.get(), "Cell Leakage Power   ==    %.3e %s\n",
               summary_leakage_power, summary_leakage_power_percentage.c_str());

  std::fprintf(f.get(), "Total Power   ==  %.3e W\n", total_power);

  LOG_INFO << "Total Power   ==  " << total_power << " W";
  return 1;
};

/**
 * @brief report json file
 *
 * @param rpt_file_name
 * @return unsigned
 */
unsigned Power::reportSummaryPowerJSON(const char* rpt_file_name,
                                       PwrAnalysisMode pwr_analysis_mode) {
  PwrReportPowerSummary report_power("", pwr_analysis_mode);
  report_power(this);
  auto& report_summary_data = report_power.get_report_summary_data();
  nlohmann::json json_report = nlohmann::json::object();
  auto& summary_json = json_report["summary"] = nlohmann::json::array();

  // lambda for print power data float to string.
  auto data_str = [](double data) { return Str::printf("%.3e", data); };
  auto data_str_f = [](double data) { return Str::printf("%.3f", data); };

  // extract module name from vertex name
  auto extract_module_name = [](const std::string& name) -> std::string {
    size_t pos = name.find('/');
    if (pos != std::string::npos) {
      return name.substr(0, pos);
    }
    return "";
  };

  std::map<PwrGroupData::PwrGroupType, std::string> group_type_to_string = {
      {PwrGroupData::PwrGroupType::kIOPad, "io_pad"},
      {PwrGroupData::PwrGroupType::kMemory, "memory"},
      {PwrGroupData::PwrGroupType::kBlackBox, "black_box"},
      {PwrGroupData::PwrGroupType::kClockNetwork, "clock_network"},
      {PwrGroupData::PwrGroupType::kRegister, "register"},
      {PwrGroupData::PwrGroupType::kComb, "combinational"},
      {PwrGroupData::PwrGroupType::kSeq, "sequential"}};

  double total_power = report_summary_data.get_total_power();

  PwrReportGroupSummaryData* report_group_data;
  FOREACH_REPORT_GROUP_DATA(&report_summary_data, report_group_data) {
    double group_total_power = report_group_data->get_total_power();
    // calc percentage
    double percentage = CalcPercentage(group_total_power / total_power);

    std::string str_percentage =
        std::string("(") + data_str_f(percentage) + std::string("%)");

    json_report["groups"].push_back({
        {"group_type",
         group_type_to_string[report_group_data->get_group_type()]},
        {"internal_power", data_str(report_group_data->get_internal_power())},
        {"switch_power", data_str(report_group_data->get_switch_power())},
        {"leakage_power", data_str(report_group_data->get_leakage_power())},
        {"total_power", data_str(report_group_data->get_total_power())},
        {"percentage", data_str_f(percentage)},
    });
  }

  auto instance_power_data_vec = getInstancePowerData();
  std::sort(instance_power_data_vec.begin(), instance_power_data_vec.end(),
            [](const IRInstancePower& a, const IRInstancePower& b) {
              return a._total_power > b._total_power;
            });

  // Helper struct for module power statistics
  struct ModuleStats {
    std::string module_name;
    double internal_power = 0.0;
    double switch_power = 0.0;
    double leakage_power = 0.0;
    double total_power = 0.0;
    double nominal_voltage = 0.0;
  };

  std::unordered_map<std::string, ModuleStats> module_stats;

  // Try to get module power data
  bool failed_extract_module_name = false;
  for (auto instance_power_data : instance_power_data_vec) {
    auto name = extract_module_name(instance_power_data._instance_name);
    if (name.empty() && !failed_extract_module_name) {
      LOG_WARNING
          << "Failed to extract module name from instance: "
          << instance_power_data._instance_name
          << ". Hierarchical naming (e.g., 'module/instance') is required "
          << " but Yosys may flatten hierarchy. "
          << "The power summary of individual modules will be stopped.";

      failed_extract_module_name = true;
      break;
    }

    module_stats[name].module_name = name;
    module_stats[name].internal_power += instance_power_data._internal_power;
    module_stats[name].switch_power += instance_power_data._switch_power;
    module_stats[name].leakage_power += instance_power_data._leakage_power;
    module_stats[name].nominal_voltage += instance_power_data._nominal_voltage;
    module_stats[name].total_power += instance_power_data._total_power;
  };

  // Get switch power
  double summary_switch_power = report_summary_data.get_net_switching_power();
  std::string summary_switch_power_percentage = data_str_f(CalcPercentage(
      report_summary_data.get_net_switching_power() / total_power));

  json_report["net_switch_power"] = data_str(summary_switch_power);
  json_report["net_switch_power_percentage"] = summary_switch_power_percentage;

  // Get internal power
  double summary_internal_power = report_summary_data.get_cell_internal_power();
  std::string summary_internal_power_percentage = data_str_f(CalcPercentage(
      report_summary_data.get_cell_internal_power() / total_power));

  json_report["cell_internal_power"] = data_str(summary_internal_power);
  json_report["cell_internal_power_percentage"] =
      summary_internal_power_percentage;

  // Get leakage power
  double summary_leakage_power = report_summary_data.get_cell_leakage_power();
  std::string summary_leakage_power_percentage = data_str_f(CalcPercentage(
      report_summary_data.get_cell_leakage_power() / total_power));

  json_report["cell_leakage_power"] = data_str(summary_leakage_power);
  json_report["cell_leakage_power_percentage"] =
      summary_leakage_power_percentage;

  // Get total power
  json_report["total_power"] = data_str(total_power);

  if (!failed_extract_module_name) {
    for (const auto& s : module_stats) {
      const auto& stats = s.second;

      auto percentage = CalcPercentage(stats.total_power / total_power);

      summary_json.push_back({
          {"module_name", stats.module_name},
          {"internal_power", data_str(stats.internal_power)},
          {"switch_power", data_str(stats.switch_power)},
          {"leakage_power", data_str(stats.leakage_power)},
          {"total_power", data_str(stats.total_power)},
          {"nominal_voltage", data_str(stats.nominal_voltage)},
          {"percentage", data_str_f(percentage)},
      });
    }
  }

  std::ofstream out_file(rpt_file_name);
  if (out_file.is_open()) {
    out_file << json_report.dump(4);  // 4 spaces indent
    LOG_INFO << "JSON report written to: " << rpt_file_name;
    out_file.close();
  } else {
    LOG_ERROR << "Failed to open JSON report file: " << rpt_file_name;
  }

  return 1;
}

/**
 * @brief report instance power
 *
 * @param rpt_file_name
 * @param pwr_analysis_mode
 * @return unsigned
 */
unsigned Power::reportInstancePower(const char* rpt_file_name,
                                    PwrAnalysisMode pwr_analysis_mode) {
  PwrReportInstance report_instance_power(rpt_file_name, pwr_analysis_mode);
  auto report_tbl =
      report_instance_power.createReportTable("Power Analysis Instance Report");

  // lambda for print power data float to string.
  auto data_str = [](double data) { return Str::printf("%.3e", data); };
  // auto data_str_f = [](double data) { return Str::printf("%.3f", data); };

  auto instance_power_data_vec = getInstancePowerData();
  std::sort(instance_power_data_vec.begin(), instance_power_data_vec.end(),
            [](const IRInstancePower& a, const IRInstancePower& b) {
              return a._total_power > b._total_power;
            });

  for (auto instance_power_data : instance_power_data_vec) {
    (*report_tbl) << instance_power_data._instance_name
                  << instance_power_data._nominal_voltage
                  << data_str(instance_power_data._internal_power)
                  << data_str(instance_power_data._switch_power)
                  << data_str(instance_power_data._leakage_power)
                  << data_str(instance_power_data._total_power)
                  << TABLE_ENDLINE;
  };

  LOG_INFO << "Instance Power Report: \n";
  LOG_INFO << "\n" << report_tbl->c_str();

  auto close_file = [](std::FILE* fp) { std::fclose(fp); };

  std::unique_ptr<std::FILE, decltype(close_file)> f(
      std::fopen(rpt_file_name, "w"), close_file);

  std::fprintf(f.get(), "Generate the report at %s\n", Time::getNowWallTime());

  std::map<PwrAnalysisMode, std::string> analysis_mode_to_string = {
      {PwrAnalysisMode::kAveraged, "Averaged"},
      {PwrAnalysisMode::kTimeBase, "TimeBase"},
      {PwrAnalysisMode::kClockCycle, "ClockCycle"}};

  std::fprintf(f.get(), "Report : %s Power\n ",
               analysis_mode_to_string[pwr_analysis_mode].c_str());

  std::fprintf(f.get(), "%s", report_tbl->c_str());

  return 1;
}

/**
 * @brief report csv file
 *
 * @param rpt_file_name
 * @return unsigned
 */
unsigned Power::reportInstancePowerCSV(const char* rpt_file_name) {
  std::ofstream csv_file(rpt_file_name);
  csv_file << "Instance Name"
           << ","
           << "Nominal Voltage"
           << ","
           << "Internal Power"
           << ","
           << "Switch Power"
           << ","
           << "Leakage Power"
           << ","
           << "Total Power"
           << "\n";
  auto data_str = [](double data) { return Str::printf("%.3e", data); };

  auto instance_power_data_vec = getInstancePowerData();
  std::sort(instance_power_data_vec.begin(), instance_power_data_vec.end(),
            [](const IRInstancePower& a, const IRInstancePower& b) {
              return a._total_power > b._total_power;
            });

  for (auto instance_power_data : instance_power_data_vec) {
    csv_file << instance_power_data._instance_name << ","
             << instance_power_data._nominal_voltage << ","
             << data_str(instance_power_data._internal_power) << ","
             << data_str(instance_power_data._switch_power) << ","
             << data_str(instance_power_data._leakage_power) << ","
             << data_str(instance_power_data._total_power) << "\n";
  };

  csv_file.close();
  return 1;
}

/**
 * @brief get instance power data.
 *
 * @return unsigned
 */
std::vector<IRInstancePower> Power::getInstancePowerData() {
  std::vector<IRInstancePower> instance_power_data;

  IRInstancePower instance_power;
  PwrGroupData* group_data;
  FOREACH_PWR_GROUP_DATA(this, group_data) {
    // skip net group data.
    if (dynamic_cast<Net*>(group_data->get_obj())) {
      continue;
    }

    // // skip the instance which power is 0.
    if (group_data->get_total_power() < 1e-15) {
      continue;
    }

    auto* inst = dynamic_cast<Instance*>(group_data->get_obj());
    instance_power._instance_name = inst->get_name();
    instance_power._nominal_voltage = group_data->get_nom_voltage();
    instance_power._internal_power = group_data->get_internal_power();
    instance_power._switch_power = group_data->get_switch_power();
    instance_power._leakage_power = group_data->get_leakage_power();
    instance_power._total_power = group_data->get_total_power();

    instance_power_data.emplace_back(std::move(instance_power));
  }

  return instance_power_data;
}
/**
 * @brief get instance power map.
 *
 * @return std::map<Instance::Coordinate, double>
 */
std::map<Instance::Coordinate, double> Power::displayInstancePowerMap() {
  LOG_INFO << "display instance power map start";

  std::map<Instance::Coordinate, double> instance_power_map;

  PwrGroupData* group_data;
  FOREACH_PWR_GROUP_DATA(this, group_data) {
    if (dynamic_cast<Net*>(group_data->get_obj())) {
      continue;
    }

    auto* inst = dynamic_cast<Instance*>(group_data->get_obj());
    instance_power_map[inst->get_coordinate().value()] =
        group_data->get_total_power();
  }

  LOG_INFO << "display instance power map end";

  return instance_power_map;
}

/**
 * @brief init power graph data
 *
 * @param
 * @return unsigned
 */
unsigned Power::initPowerGraphData() {
  Sta* ista = Sta::getOrCreateSta();
  Power* ipower = Power::getOrCreatePower(&(ista->get_graph()));

  {
    ieda::Stats stats;

    // set fastest clock for default toggle
    auto* fastest_clock = ista->getFastestClock();
    ipower::PwrClock pwr_fastest_clock(fastest_clock->get_clock_name(),
                                       fastest_clock->getPeriodNs());
    // get sta clocks
    auto clocks = ista->getClocks();

    ipower->setupClock(std::move(pwr_fastest_clock), std::move(clocks));

    LOG_INFO << "build graph and seq graph start";
    // build power graph
    buildGraph();

    // build seq graph
    buildSeqGraph();

    LOG_INFO << "build graph and seq graph end";
    double memory_delta = stats.memoryDelta();
    LOG_INFO << "build graph and seq graph memory usage " << memory_delta
             << "MB";
    double time_delta = stats.elapsedRunTime();
    LOG_INFO << "build graph and seq graph time elapsed " << time_delta << "s";
  }

  {
    ieda::Stats stats;
    LOG_INFO << "power annotate vcd start";
    // std::pair begin_end = {0, 50000000};
    // readVCD("/home/taosimin/T28/vcd/asic_top.vcd", "u0_asic_top",
    //                 begin_end);
    // annotate toggle sp
    annotateToggleSP();

    LOG_INFO << "power vcd annotate end";
    double memory_delta = stats.memoryDelta();
    LOG_INFO << "power vcd annotate memory usage " << memory_delta << "MB";
    double time_delta = stats.elapsedRunTime();
    LOG_INFO << "power vcd annotate time elapsed " << time_delta << "s";
  }

  return 1;
}

/**
 * @brief init toggle sp data.
 *
 * @return unsigned
 */
unsigned Power::initToggleSPData() {
  {
    ieda::Stats stats;
    LOG_INFO << "power propagation start";

    // firstly levelization.
    Vector<std::function<unsigned(PwrSeqGraph*)>> seq_funcs = {
        PwrCheckPipelineLoop(), PwrLevelizeSeq()};
    auto& the_seq_graph = get_power_seq_graph();
    for (auto& func : seq_funcs) {
      the_seq_graph.exec(func);
    }

    // secondly propagation toggle and sp.
    Vector<std::function<unsigned(PwrGraph*)>> prop_funcs = {
        PwrPropagateConst(), PwrPropagateToggleSP(), PwrPropagateClock()};
    auto& the_pwr_graph = get_power_graph();
    for (auto& func : prop_funcs) {
      the_pwr_graph.exec(func);
    }

    LOG_INFO << "power propagation end";
    double memory_delta = stats.memoryDelta();
    LOG_INFO << "power propagation memory usage " << memory_delta << "MB";
    double time_delta = stats.elapsedRunTime();
    LOG_INFO << "power propagation time elapsed " << time_delta << "s";
  }

  return 1;
}

std::optional<std::pair<std::string, std::string>> BackupPwrFiles(
    std::string output_dir, bool is_copy) {
  if (!is_copy) {
    return std::nullopt;
  }

  std::string now_time = Time::getNowWallTime();
  std::string tmp = Str::replace(now_time, ":", "_");
  std::string copy_design_work_space =
      Str::printf("%s_pwr_%s", output_dir.c_str(), tmp.c_str());

  if (std::filesystem::exists(output_dir)) {
    std::filesystem::create_directories(copy_design_work_space);
  }

  return std::pair{copy_design_work_space, tmp};
};

// copy file to copy directory
void CopyFile(
    std::optional<std::pair<std::string, std::string>> copy_design_work_space,
    std::string output_dir, std::string file_to_be_copy) {
  auto base_name = std::filesystem::path(file_to_be_copy).stem().string();
  auto extension = std::filesystem::path(file_to_be_copy).extension().string();

  // dest dir workspace and copy time stamp.
  auto copy_work_space = copy_design_work_space->first;
  auto copy_time = copy_design_work_space->second;

  std::string dest_file_name =
      Str::printf("%s/%s_%s%s", copy_work_space.c_str(), base_name.c_str(),
                  copy_time.c_str(), extension.c_str());

  std::string src_file =
      Str::printf("%s/%s", output_dir.c_str(), file_to_be_copy.c_str());
  if (std::filesystem::exists(src_file)) {
    std::filesystem::copy_file(
        src_file, dest_file_name,
        std::filesystem::copy_options::overwrite_existing);
  }
};

/**
 * @brief report power
 *
 * @return unsigned
 */
unsigned Power::reportPower(bool is_copy) {
  Sta* ista = Sta::getOrCreateSta();

  ieda::Stats stats;

  std::string output_dir = get_design_work_space();
  if (output_dir.empty()) {
    output_dir = ista->get_design_work_space();
  }

  LOG_INFO << "power report start, output dir: " << output_dir;

  if (output_dir.empty()) {
    LOG_ERROR << "The design work space is not set.";
    return 0;
  }

  auto backup_work_space = BackupPwrFiles(output_dir, is_copy);
  _backup_work_dir = backup_work_space;
  std::filesystem::create_directories(output_dir);
  const char* design_name = ista->get_design_name().c_str();

  {
    std::string file_name = Str::printf("%s.pwr", design_name);
    if (is_copy) {
      CopyFile(backup_work_space, output_dir, file_name);
    }
    std::string output_path = output_dir + "/" + file_name;
    reportSummaryPower(output_path.c_str(), PwrAnalysisMode::kAveraged);
  }

  {
    std::string file_name = Str::printf("%s_%s.pwr", design_name, "instance");

    if (is_copy) {
      CopyFile(backup_work_space, output_dir, file_name);
    }

    std::string output_path = output_dir + "/" + file_name;
    reportInstancePower(output_path.c_str(), PwrAnalysisMode::kAveraged);
  }

  {
    std::string file_name = Str::printf("%s_%s.csv", design_name, "instance");

    if (is_copy) {
      CopyFile(backup_work_space, output_dir, file_name);
    }
    std::string output_path = output_dir + "/" + file_name;
    reportInstancePowerCSV(output_path.c_str());
  }

  if (isJsonReportEnabled()) {
    std::string file_name = Str::printf("%s.pwr.json", design_name);
    if (is_copy) {
      CopyFile(backup_work_space, output_dir, file_name);
    }

    std::string output_path = output_dir + "/" + file_name;
    reportSummaryPowerJSON(output_path.c_str(), PwrAnalysisMode::kAveraged);
  }

  if (0) {
    using json = nlohmann::ordered_json;

    json graph_json;
    PwrDumpGraphJson dump_graph_json(graph_json);
    auto &the_graph = get_power_graph();
    dump_graph_json(&the_graph);

    std::string graph_json_file_name = Str::printf(
        "%s/%s_pwr_graph.json", output_dir.c_str(), design_name);

    std::ofstream out_file(graph_json_file_name);
    if (out_file.is_open()) {
      out_file << graph_json.dump(4);  // 4 spaces indent
      LOG_INFO << "JSON report written to: " << graph_json_file_name;
      out_file.close();
    } else {
      LOG_ERROR << "Failed to open JSON report file: " << graph_json_file_name;
    }
  }

  // for debug
  if (0) {
    dumpGraph();
  }

  LOG_INFO << "power report end, output dir: " << output_dir;
  double memory_delta = stats.memoryDelta();
  LOG_INFO << "power report memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "power report time elapsed " << time_delta << "s";

  // restart timer.
  Time::start();

  return 1;
}

/**
 * @brief run report ipower
 *
 * @return unsigned
 */
unsigned Power::runCompleteFlow() {
  Sta* ista = Sta::getOrCreateSta();
  Power::getOrCreatePower(&(ista->get_graph()));

  initPowerGraphData();
  initToggleSPData();

  updatePower();
  reportPower();
  return 1;
}

/**
 * @brief get the toggle and vdd data of a net.
 *
 * @param net_name
 * @return std::pair<double, double>
 */
std::pair<double, double> Power::getNetToggleAndVoltageData(
    const char* net_name) {
  auto* sta_graph = _power_graph.get_sta_graph();
  auto* nl = sta_graph->get_nl();
  auto* the_net = nl->findNet(net_name);

  auto* driver_obj = the_net->getDriver();
  if (!driver_obj || the_net->getLoads().empty()) {
    return {0.0, 0.0};
  }

  if (driver_obj->isPort() && ((the_net->getLoads().size() == 1) &&
                               the_net->getLoads().front()->isPort())) {
    return {0.0, 0.0};
  }

  auto driver_sta_vertex = sta_graph->findVertex(driver_obj);

  PwrVertex* driver_pwr_vertex = nullptr;
  if (driver_sta_vertex) {
    driver_pwr_vertex = _power_graph.staToPwrVertex(*driver_sta_vertex);
  } else {
    return {0.0, 0.0};
  }

  // get VDD
  auto driver_voltage = driver_pwr_vertex->getDriveVoltage();
  if (!driver_voltage) {
    LOG_FATAL << "can not get driver voltage.";
  }
  double vdd = driver_voltage.value();

  // get Toggle
  double toggle = driver_pwr_vertex->getToggleData(std::nullopt);

  return {toggle, vdd};
}

/**
 * @brief read pg spef file.
 *
 * @param spef_file
 * @return unsigned
 */
unsigned Power::readPGSpef(const char* spef_file) {
  LOG_INFO << "read pg spef start.";
  _ir_analysis.readSpef(spef_file);
  set_rust_pg_rc_data(_ir_analysis.get_rc_data());
  LOG_INFO << "read pg spef end.";
  return 1;
}

/**
 * @brief report IR drop in table.
 *
 * @param rpt_file_name
 * @return unsigned
 */
unsigned Power::reportIRDropTable(const char* rpt_file_name) {
  auto create_report_table = [](const char* title) {
    auto report_tbl = std::make_unique<PwrReportInstanceTable>(title);

    (*report_tbl) << TABLE_HEAD;
    /* Fill each cell with operator[] */
    (*report_tbl)[0][0] = "Instance Name";
    (*report_tbl)[0][1] = "IR Drop";
    (*report_tbl) << TABLE_ENDLINE;

    return report_tbl;
  };

  Time::stop();
  double elapsed_time = Time::elapsedTime();
  LOG_INFO << "iIR total elapsed time: " << elapsed_time << " seconds";
  auto close_file = [](std::FILE* fp) { std::fclose(fp); };

  std::unique_ptr<std::FILE, decltype(close_file)> f(
      std::fopen(rpt_file_name, "w"), close_file);

  std::fprintf(f.get(), "Generate the report at %s\n", Time::getNowWallTime());
  std::fprintf(f.get(), "iIR elapsed time: %.2f seconds.\n\n", elapsed_time);

  auto pg_net_bump_node_loc = _ir_analysis.get_net_bump_node_locs();
  for (auto [pg_net_name, net_bump_node_loc] : pg_net_bump_node_loc) {
    std::fprintf(f.get(), "PG Net %s bump node loc: (%.3f %.3f %s)\n",
                 pg_net_name.c_str(), net_bump_node_loc.first.first,
                 net_bump_node_loc.first.second,
                 net_bump_node_loc.second.c_str());
  }

  double nominal_voltage = getNominalVoltage();
  std::fprintf(f.get(), "Nominal Voltage: %.3f V\n", nominal_voltage);

  auto data_str = [](double data) { return Str::printf("%.3e", data); };
  auto net_to_instance_ir_drop = getNetInstanceIRDrop();

  for (auto [net_name, instance_to_ir_drop] : net_to_instance_ir_drop) {
    auto report_tbl = create_report_table(
        Str::printf("Net %s IR Drop Report", net_name.c_str()));
    // sort
    std::vector<std::pair<std::string, double>> ir_drop_vec(
        instance_to_ir_drop.begin(), instance_to_ir_drop.end());
    std::sort(ir_drop_vec.begin(), ir_drop_vec.end(),
              [](const auto& a, const auto& b) {
                return a.second > b.second;  // descending order
              });

    for (auto& [instance_name, ir_drop] : ir_drop_vec) {
      (*report_tbl) << instance_name << data_str(ir_drop) << TABLE_ENDLINE;
    }

    std::fprintf(f.get(), "\nNet %s max IR Drop: %s %f V\n", net_name.c_str(),
                 ir_drop_vec.front().first.c_str(), ir_drop_vec.front().second);
    std::fprintf(f.get(), "Net %s min IR Drop: %s %f V\n", net_name.c_str(),
                 ir_drop_vec.back().first.c_str(), ir_drop_vec.back().second);

    std::fprintf(f.get(), "Report : Net %s IR Drop Report, Unit V\n",
                 net_name.c_str());
    std::fprintf(f.get(), "%s\n", report_tbl->c_str());
    LOG_INFO << "Instance IR Drop Report for net " << net_name << " :\n"
             << report_tbl->c_str();
  }

  return 1;
}

/**
 * @brief report IR Drop in csv file.
 *
 * @param rpt_file_name
 * @return unsigned
 */
unsigned Power::reportIRDropCSV(const char* rpt_file_name,
                                std::string net_name) {
  std::ofstream csv_file(rpt_file_name);
  csv_file << "Instance Name"
           << ","
           << "IR Drop"
           << "\n";
  auto data_str = [](double data) { return Str::printf("%.3e", data); };
  auto net_to_instance_ir_drop = getNetInstanceIRDrop();
  auto instance_to_ir_drop = net_to_instance_ir_drop[net_name];

  for (auto& [instance_name, ir_drop] : instance_to_ir_drop) {
    csv_file << instance_name << "," << data_str(ir_drop) << "\n";
  }

  csv_file.close();

  return 1;
}

/**
 * @brief run ir analysis.
 *
 * @return unsigned
 */
unsigned Power::runIRAnalysis(std::string power_net_name) {
  CPU_PROF_START(0);
  LOG_INFO << "run IR analysis start";
  // set power data.
  std::vector<IRInstancePower> instance_power_data = getInstancePowerData();
  _ir_analysis.setInstancePowerData(std::move(instance_power_data));

  // calc ir drop.
  _ir_analysis.solveIRDrop(power_net_name.c_str());

  LOG_INFO << "run IR analysis end";

  CPU_PROF_END(0, "run IR analysis");

  return 1;
}

/**
 * @brief report ir analysis.
 *
 * @return unsigned
 */
unsigned Power::reportIRAnalysis(bool is_copy) {
  LOG_INFO << "report IR analysis start";
  Sta* ista = Sta::getOrCreateSta();
  std::string output_dir = get_design_work_space();
  if (output_dir.empty()) {
    output_dir = ista->get_design_work_space();
  }

  if (output_dir.empty()) {
    LOG_ERROR << "The design work space is not set.";
    return 0;
  }

  // report table file.
  {
    std::string table_file_name =
        Str::printf("%s.ir", ista->get_design_name().c_str());

    if (is_copy) {
      if (!_backup_work_dir) {
        _backup_work_dir = BackupPwrFiles(output_dir, is_copy);
      }

      CopyFile(_backup_work_dir, output_dir, table_file_name);
    }

    std::string output_path = output_dir + "/" + table_file_name;

    // report in IR drop csv.
    reportIRDropTable(output_path.c_str());

    LOG_INFO << "output ir drop report: " << output_path;
  }

  auto net_to_instance_ir_drop = getNetInstanceIRDrop();
  for (auto [net_name, instance_ir_drop] : net_to_instance_ir_drop) {
    // report csv file.

    std::string csv_file_name =
        Str::printf("%s_%s_%s.csv", ista->get_design_name().c_str(),
                    net_name.c_str(), "ir_drop");

    if (is_copy) {
      if (!_backup_work_dir) {
        _backup_work_dir = BackupPwrFiles(output_dir, is_copy);
      }

      CopyFile(_backup_work_dir, output_dir, csv_file_name);
    }

    std::string output_path = output_dir + "/" + csv_file_name;

    // report in IR drop csv.
    reportIRDropCSV(output_path.c_str(), net_name);

    LOG_INFO << "output ir drop csv report: " << output_path;
  }

  LOG_INFO << "report IR analysis end";
  return 1;
}

}  // namespace ipower
