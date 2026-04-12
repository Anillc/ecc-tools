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
 * @file StaDelayPropagation.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The implemention of delay propagation.
 * @version 0.1
 * @date 2021-04-10
 */
#include "StaDelayPropagation.hh"

#include <optional>

#include "StaArc.hh"
#include "ThreadPool/ThreadPool.h"
#include "Type.hh"
#include "delay/ElmoreDelayCalc.hh"
#include "delay/ReduceDelayCal.hh"
#include "netlist/Pin.hh"
#include "netlist/Port.hh"

namespace ista {
inline int timingSenseToInt(ista::LibArc::TimingSense sense) {
    switch (sense) {
        case ista::LibArc::TimingSense::kPositiveUnate: return 1;
        case ista::LibArc::TimingSense::kNegativeUnate: return -1;
        case ista::LibArc::TimingSense::kNonUnate:      return 0;
        case ista::LibArc::TimingSense::kDefault:       return 1;
        default:                                      return 1;
    }
}
/**
 * @brief The delay propagation from  the arc.
 *
 * @param the_arc
 * @return unsigned  1 if success, 0 else fail.
 */
unsigned StaDelayPropagation::operator()(StaArc* the_arc) {
  auto flip_trans_type = [](auto trans_type) {
    return trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  };

  auto construct_delay_data = [this](AnalysisMode delay_type,
                                     TransType trans_type, StaArc* own_arc,
                                     int delay) {
    StaArcDelayData* arc_delay = nullptr;
    if (isIncremental()) {
      arc_delay = own_arc->getArcDelayData(delay_type, trans_type);
    }

    if (!arc_delay) {
      arc_delay = new StaArcDelayData(delay_type, trans_type, own_arc, delay);
      own_arc->addData(arc_delay);
    }

    if (isIncremental()) {
      arc_delay->set_arc_delay(delay);
    }
  };

  unsigned is_ok = 1;

  auto* src_vertex = the_arc->get_src();
  auto* snk_vertex = the_arc->get_snk();
  auto* obj = snk_vertex->get_design_obj();
  auto* the_net = obj->get_net();

  StaData* slew_data;
  FOREACH_SLEW_DATA(src_vertex, slew_data) {
    auto analysis_mode = slew_data->get_delay_type();
    auto trans_type = slew_data->get_trans_type();
    if (analysis_mode == get_analysis_mode() ||
        AnalysisMode::kMaxMin == get_analysis_mode()) {
      auto* src_slew_data = dynamic_cast<StaSlewData*>(slew_data);
      auto in_slew_fs = src_slew_data->get_slew();
      auto in_slew = FS_TO_NS(in_slew_fs);

      if (the_arc->isInstArc()) {
        auto* lib_arc = dynamic_cast<StaInstArc*>(the_arc)->get_lib_arc();
        auto* lib_arc_set =
            dynamic_cast<StaInstArc*>(the_arc)->get_lib_arc_set();
        /*The check arc is the end of the recursion .*/
        if (the_arc->isCheckArc()) {
          // Since slew is fitter accord trigger type, May be do not need below
          // code
          if ((src_vertex->isRisingTriggered() && IS_FALL(trans_type)) ||
              (src_vertex->isFallingTriggered() && IS_RISE(trans_type))) {
            continue;
          }

          StaData* snk_slew_data;
          FOREACH_SLEW_DATA(snk_vertex, snk_slew_data) {
            if (snk_slew_data->get_delay_type() != analysis_mode) {
              continue;
            }

            auto snk_trans_type = snk_slew_data->get_trans_type();
            auto snk_slew_fs =
                dynamic_cast<StaSlewData*>(snk_slew_data)->get_slew();
            auto snk_slew = FS_TO_NS(snk_slew_fs);
            auto delay_values = lib_arc_set->getDelayOrConstrainCheckNs(trans_type,
                snk_trans_type, in_slew, snk_slew);
            double delay_ns = analysis_mode == AnalysisMode::kMax
                                  ? delay_values.front()
                                  : delay_values.back();
            auto delay = NS_TO_FS(delay_ns);
            construct_delay_data(analysis_mode, snk_trans_type, the_arc, delay);
          }

        } else if (the_arc->isDelayArc()) {
          auto* rc_net = getSta()->getRcNet(the_net);

          auto out_trans_type = the_arc->isNegativeArc()
                                    ? flip_trans_type(trans_type)
                                    : trans_type;
          auto trans_to_index = [](TransType trans_type) -> int {
            return static_cast<int>(trans_type) - 1;
          };
          auto* owner_lib = lib_arc->get_owner_cell()->get_owner_lib();

          std::array<double, 2> load_array;  // rise, fall load.

          for (auto load_trans_type : {TransType::kRise, TransType::kFall}) {
            auto load_pf =
                rc_net ? rc_net->load(analysis_mode, out_trans_type)
                       : the_net->getLoad(analysis_mode, out_trans_type);

            double load{0};
            if (owner_lib->get_cap_unit() == CapacitiveUnit::kFF) {
              load = PF_TO_FF(load_pf);
            } else if (owner_lib->get_cap_unit() == CapacitiveUnit::kPF) {
              load = load_pf;
            }

            load_array[trans_to_index(load_trans_type)] = load;
          }

          // fix the timing type not match the trans type, which would lead to
          // crash.
          if (!lib_arc_set->isMatchTimingType(out_trans_type)) {
            continue;
          }

          // assure delay values sort by descending order.
          auto delay_values = lib_arc_set->getDelayOrConstrainCheckNs(trans_type,
              out_trans_type, in_slew,
              load_array[trans_to_index(out_trans_type)]);
          double delay_ns = analysis_mode == AnalysisMode::kMax
                                ? delay_values.front()
                                : delay_values.back();
          auto delay = NS_TO_FS(delay_ns);
          auto* src_pin = dynamic_cast<Pin*>(src_vertex->get_design_obj());
          auto* snk_pin = dynamic_cast<Pin*>(snk_vertex->get_design_obj());

          if (src_pin && snk_pin) {
              auto should_log_arc_net_choice =
                  [](const std::string& from_pin_name,
                     const std::string& to_pin_name) -> bool {
                return (from_pin_name.find("FE_DBTC149_n_2224:A") !=
                            std::string::npos &&
                        to_pin_name.find("FE_DBTC149_n_2224:Y") !=
                            std::string::npos) ||
                       (from_pin_name.find("g63378:A2") != std::string::npos &&
                        to_pin_name.find("g63378:Y") != std::string::npos);
              };

              if (should_log_arc_net_choice(src_pin->getFullName(),
                                            snk_pin->getFullName())) {
                auto* src_net = src_pin->get_net();
                auto* snk_net = snk_pin->get_net();
                auto dump_net_load_pf =
                    [&](Net* net, const char* net_role) {
                      if (!net) {
                        LOG_INFO << "[ArcNetDebug] " << src_pin->getFullName()
                                 << " -> " << snk_pin->getFullName() << " "
                                 << net_role << "=<null>";
                        return;
                      }

                      auto* role_rc_net = getSta()->getRcNet(net);
                      auto role_load_pf =
                          role_rc_net ? role_rc_net->load(analysis_mode,
                                                          out_trans_type)
                                      : net->getLoad(analysis_mode,
                                                     out_trans_type);
                      LOG_INFO << "[ArcNetDebug] " << src_pin->getFullName()
                               << " -> " << snk_pin->getFullName() << " "
                               << net_role << "=" << net->get_name()
                               << " load_pf=" << role_load_pf
                               << " has_rc_net=" << (role_rc_net != nullptr);
                    };

                LOG_INFO << "[ArcNetDebug] inspect arc "
                         << src_pin->getFullName() << " -> "
                         << snk_pin->getFullName()
                         << " analysis_mode="
                         << (analysis_mode == AnalysisMode::kMax ? "Max"
                                                                : "Min")
                         << " in_trans="
                         << (trans_type == TransType::kRise ? "Rise" : "Fall")
                         << " out_trans="
                         << (out_trans_type == TransType::kRise ? "Rise"
                                                                : "Fall");
                dump_net_load_pf(src_net, "src_net");
                dump_net_load_pf(snk_net, "snk_net");
                dump_net_load_pf(the_net, "chosen_net");
              }

              // 填充调试信息结构体
              ArcDebugInfo info;
              info.inst_name = src_pin->get_own_instance()->get_name();
              info.from_pin = src_pin->getFullName();
              info.to_pin = snk_pin->getFullName();
            if (info.from_pin.find("_23155_:A") != std::string::npos
                && info.to_pin.find("_23155_:Y") != std::string::npos) {

                }
              info.analysis_mode = (analysis_mode == AnalysisMode::kMax ? "Max" : "Min");
              info.transition = (out_trans_type == TransType::kRise ? "Rise" : "Fall");
              info.in_slew_ns = in_slew;
              // Debug export should stay on the internal pF basis so Python-side
              // reports can compare iEDA and PyPlaceDB loads directly.
              auto debug_load_cap = load_array[trans_to_index(out_trans_type)];
              if (owner_lib->get_cap_unit() == CapacitiveUnit::kFF) {
                debug_load_cap = FF_TO_PF(debug_load_cap);
              } else if (owner_lib->get_cap_unit() == CapacitiveUnit::kF) {
                debug_load_cap = F_TO_PF(debug_load_cap);
              }
              info.load_cap = debug_load_cap;
              info.delay_ns = delay_ns;
              info.timing_sense = timingSenseToInt(lib_arc->get_timing_sense());

              // 将信息存入线程安全的管理器
              ArcDebugDataManager::getInstance().addArcInfo(info);
          }
          // DEBUG
          construct_delay_data(analysis_mode, out_trans_type, the_arc, delay);
          /*The unate arc should split two.*/
          if (!the_arc->isUnateArc() || the_arc->isTwoTypeSenseArc() || src_vertex->is_clock()) {
            auto out_trans_type1 = flip_trans_type(trans_type);

            // fix the timing type not match the trans type, which would lead to
            // crash.
            if (!lib_arc_set->isMatchTimingType(out_trans_type1)) {
              continue;
            }
            auto delay_values = lib_arc_set->getDelayOrConstrainCheckNs(trans_type,
                out_trans_type1, in_slew,
                load_array[trans_to_index(out_trans_type1)]);
            double delay1_ns = analysis_mode == AnalysisMode::kMax
                                  ? delay_values.front()
                                  : delay_values.back();
            auto delay1 = NS_TO_FS(delay1_ns);

            construct_delay_data(analysis_mode, out_trans_type1, the_arc,
                                 delay1);
          }
        } else if (the_arc->isMpwArc()) {
          // TODO(to taosimin) fix mpw arc
          return is_ok;
        }
      } else {  // net arc
        auto* rc_net = getSta()->getRcNet(the_net);
        auto output_current = src_slew_data->get_output_current_data();
        auto net_delay = rc_net ? rc_net->delay(*obj, in_slew, output_current,
                                                analysis_mode, trans_type)
                                : std::nullopt;
        auto delay_ps = net_delay ? net_delay->first : 0.0;
        auto delay = PS_TO_FS(delay_ps);
        construct_delay_data(analysis_mode, trans_type, the_arc, delay);

        if (rc_net) {
          auto* arnoldi_rc_net = dynamic_cast<ArnoldiNet*>(rc_net);
          if (arnoldi_rc_net && net_delay) {
            auto* net_arc = dynamic_cast<StaNetArc*>(the_arc);
            auto node_waveform = net_delay->second;
            if (output_current) {
              auto [total_time, num_points] =
                  (*output_current)->getSimulationTotalTimeAndNumPoints();
              double step_time_ns = total_time / (num_points - 1);

              std::vector<Waveform> waveforms;
              using RowIdx = decltype(node_waveform.rows());
              for (RowIdx i = 0; i < node_waveform.rows(); ++i) {
                waveforms.emplace_back(step_time_ns, node_waveform.row(i));
              }

              auto* arc_waveform_data =
                  new StaArcWaveformData(analysis_mode, trans_type,
                                         src_slew_data, std::move(waveforms));
              net_arc->addWaveformData(arc_waveform_data);
            }
          }
        }
      }
    }
  }

  return is_ok;
}

/**
 * @brief The delay propagation from the vertex.
 *
 * @param the_vertex
 * @return unsigned 1 if success, 0 else fail.
 */
unsigned StaDelayPropagation::operator()(StaVertex* the_vertex) {
  std::lock_guard<std::mutex> lk(the_vertex->get_fwd_mutex());
  if (the_vertex->is_delay_prop() || the_vertex->is_const()) {
    return 1;
  }

  unsigned is_ok = 1;

  if ((the_vertex->is_clock() && the_vertex->is_ideal_clock_latency()) ||
      (the_vertex->is_port() && the_vertex->is_start()) ||
      the_vertex->is_sdc_clock_pin() || the_vertex->get_snk_arcs().empty()) {
    the_vertex->set_is_delay_prop();

    // set_is_trace_path();

    if (isTracePath()) {
      addTracePathVertex(the_vertex);
    }

    return is_ok;
  }

  FOREACH_SNK_ARC(the_vertex, snk_arc) {
    if (!snk_arc->isDelayArc()) {
      // calculate the check arc constrain value.
      if (snk_arc->isCheckArc()) {
        snk_arc->exec(*this);
      }
      continue;
    }

    if (snk_arc->is_loop_disable()) {
      continue;
    }

    auto* src_vertex = snk_arc->get_src();
    if (!src_vertex->exec(*this)) {
      return 0;
    }

    is_ok = snk_arc->exec(*this);
    if (!is_ok) {
      LOG_FATAL << "delay propgation error";
      break;
    }
  }

  if (isTracePath()) {
    addTracePathVertex(the_vertex);
  }

  the_vertex->set_is_delay_prop();

  return is_ok;
}

/**
 * @brief The delay propagation from the graph port vertex.
 *
 * @param the_graph
 * @return unsigned 1 if success, 0 else fail.
 */
unsigned StaDelayPropagation::operator()(StaGraph* the_graph) {
  LOG_INFO << "delay propagation start";
  unsigned is_ok = 1;

  {
#if 1
    // create thread pool
    unsigned num_threads = getNumThreads();
    ThreadPool pool(num_threads);
    StaVertex* end_vertex;

    FOREACH_END_VERTEX(the_graph, end_vertex) {
      if (end_vertex->get_snk_arcs().empty()) {
        continue;
      }
      // enqueue and store future
      pool.enqueue([](StaFunc& func,
                      StaVertex* end_vertex) { return end_vertex->exec(func); },
                   *this, end_vertex);
    }

#else

    StaVertex* end_vertex;
    FOREACH_END_VERTEX(the_graph, end_vertex) {
      if (end_vertex->get_snk_arcs().empty()) {
        continue;
      }
      is_ok = end_vertex->exec(*this);
      if (!is_ok) {
        break;
      }

      if (isTracePath()) {
        PrintTraceRecord();
        reset_is_trace_path();
      }
    }

#endif
  }

  LOG_INFO << "delay propagation end";

  return is_ok;
}

}  // namespace ista
