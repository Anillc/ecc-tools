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

#include "StaDataPropagation.hh"
#include "StaDelayPropagation.hh"
#include "StaSlewPropagation.hh"

namespace ista {

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

        _interface_logic_endpoints.emplace_back(the_vertex);

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
        _logic_clkpoint_to_port.insert(the_vertex, _current_port_vertex);
        _port_to_logic_clkpoint.insert(_current_port_vertex, the_vertex);
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
    if ((_state == kPropagateSlew) && (the_vertex->is_slew_prop())) {
      return 1;
    } else if ((_state == kPropagateDelay) && (the_vertex->is_delay_prop())) {
      return 1;
    } else if ((_state == kPropagateATFromPort) && (the_vertex->is_fwd())) {
      return 1;
    }

    if (the_vertex->is_port()) {
      // set init slew or AT.
      auto* the_port = the_vertex->get_design_obj();
      if (the_port->isInput()) {
        if (_state == kPropagateSlew) {
          the_vertex->initSlewData();  // TODO(to taosimin) need decide the
                                       // discrete  init slew data point.
        } else if (_state == kPropagateATFromPort) {
          the_vertex->initPathDelayData();
        }

        return 1;
      }
    }

    // bwd
    FOREACH_SNK_ARC(the_vertex, snk_arc) {
      if (snk_arc->isMpwArc()) {
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
    // output port need propagate slew too, for construct transition table.
    slew_propagation.set_propagate_output_port();
    slew_propagation(the_arc);
  } else if (_state == kPropagateDelay) {
    // propagate the delay along the arc.
    StaDelayPropagation delay_propagation;
    delay_propagation(the_arc);
  } else if (_state == kPropagateATFromPort) {
    // propagate the AT along the arc.
    StaFwdPropagation fwd_propagation;
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
  // first collect the interface logic endpoint.
  collectInterfaceLogicEndPoint(the_graph);

  // then propagate the slew from the port to the first sequential cell.
  propagateSlew(the_graph);

  // then propagate the delay from the port to the first sequential cell.
  propagateDelay(the_graph);

  // then propagate the AT from port to the first sequential cell.
  propagateATFromPort(the_graph);

  // then back propagate the RT from endpoint to port.
  backPropagateRTToPort(the_graph);

  // finaly gen the timing model.
  genTimingModel(the_graph, _model_path.c_str());

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
      _interface_logic_endpoints.emplace_back(port_vertex);
    }
  }

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
  _state = kPropagateSlew;
  for (auto* the_end_point : _interface_logic_endpoints) {
    (*this)(the_end_point);
  }

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
  _state = kPropagateDelay;
  for (auto* the_end_point : _interface_logic_endpoints) {
    (*this)(the_end_point);
  }

  return 1;
}

/**
 * @brief propagate AT from port to the first sequential cell..
 *
 * @param port_vertex
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateATFromPort(StaGraph* the_graph) {
  _state = kPropagateATFromPort;
  for (auto* the_end_point : _interface_logic_endpoints) {
    (*this)(the_end_point);
  }
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
  _state = kBackPropagateRTToPort;
  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) { (*this)(port_vertex); }
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
  _state = kGenTimingModel;

  std::string lib_model_path = model_path;
  std::string model_name = model_path;
  size_t pos = lib_model_path.find_last_of("/");
  if (pos != std::string::npos) {
    model_name = lib_model_path.substr(pos + 1);
  }

  _design_timing_model = std::make_unique<LibLibrary>(model_name.c_str());
  auto design_timing_cell = std::make_unique<LibCell>(
      the_graph->get_nl()->get_name(), _design_timing_model.get());

  // construct the data port to clock port check arc.
  auto construct_port_check_arc = [&design_timing_cell, this](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    if (!_port_to_logic_endpoint.contains(port_vertex)) {
      return;
    }

    auto* logic_endpoint = _port_to_logic_endpoint.values(port_vertex)
                               .front();  // TODO(to taosimin), fix me may be
                                          // more than one endpoint
    auto* endpoint_check_arc = logic_endpoint->getCheckArc(analysis_mode);
    auto* clk_point = endpoint_check_arc->get_src();
    auto* clock_port_vertex = _logic_clkpoint_to_port.values(clk_point).front();

    // construct the constrain arc.
    auto endpoint_rise_at = logic_endpoint->getArriveTimeNs(
        analysis_mode, TransType::kRise);  // TODO(to taosimin), should match
                                           // the propagated port vertex.
    auto endpoint_fall_at =
        logic_endpoint->getArriveTimeNs(analysis_mode, TransType::kFall);

    auto trigger_type = endpoint_check_arc->isRisingEdgeCheck()
                            ? TransType::kRise
                            : TransType::kFall;
    auto clk_point_at = clk_point->getArriveTimeNs(analysis_mode, trigger_type);
    auto rise_constrain_value = FS_TO_NS(
        endpoint_check_arc->get_arc_delay(analysis_mode, TransType::kRise));
    auto fall_constrain_value = FS_TO_NS(
        endpoint_check_arc->get_arc_delay(analysis_mode, TransType::kFall));

    double port_rise_constrain_value =
        rise_constrain_value + (*endpoint_rise_at - *clk_point_at);
    double port_fall_constrain_value =
        fall_constrain_value + (*endpoint_fall_at - *clk_point_at);

    auto lib_arc = std::make_unique<LibArc>();
    lib_arc->set_snk_port(port_vertex->getName().c_str());
    lib_arc->set_src_port(clock_port_vertex->getName().c_str());
    std::string timing_type =
        (analysis_mode == AnalysisMode::kMax)
            ? (endpoint_check_arc->isRisingTriggerArc() ? "setup_rising"
                                                        : "setup_falling")
            : (endpoint_check_arc->isRisingTriggerArc() ? "hold_rising"
                                                        : "hold_falling");
    lib_arc->set_timing_type(timing_type.c_str());

    // construct the timing data.
    auto table_rise_value =
        std::make_unique<LibFloatValue>(port_rise_constrain_value);
    auto table_fall_value =
        std::make_unique<LibFloatValue>(port_fall_constrain_value);

    auto lib_rise_table = std::make_unique<LibTable>(
        LibTable::TableType::kRiseConstrain,
        nullptr);  // TODO(to taosimin), construct the table template, timing
                   // sense
    lib_rise_table->addTableValue(std::move(table_rise_value));

    auto lib_fall_table = std::make_unique<LibTable>(
        LibTable::TableType::kFallConstrain,
        nullptr);  // TODO(to taosimin), construct the table template, timing
                   // sense
    lib_fall_table->addTableValue(std::move(table_fall_value));

    auto check_model = std::make_unique<LibCheckTableModel>();
    check_model->addTable(std::move(lib_rise_table));
    check_model->addTable(std::move(lib_fall_table));

    lib_arc->set_table_model(std::move(check_model));
    lib_arc->set_owner_cell(design_timing_cell.get());
    design_timing_cell->addLibertyArc(std::move(lib_arc));
  };

  // construct the data input port to output port delay arc.
  auto construct_port_delay_arc = [&design_timing_cell, this](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    auto lib_arc = std::make_unique<LibArc>();
    auto delay_model = std::make_unique<LibDelayTableModel>();
    FOREACH_TRANS(trans) {
      auto* delay_data =
          port_vertex->getWorstPathDelayData(analysis_mode, trans);
      if (!delay_data) {
        continue;
      }
      auto path_data = delay_data->getPathData();
      auto* start_vertex = path_data.top()->get_own_vertex();
      auto* slew_data = port_vertex->getWorstSlewDataFromStart(
          analysis_mode, trans, start_vertex);
      auto clock_ports = _logic_clkpoint_to_port.values();

      // input port to output port.
      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_src_port(start_vertex->getName().c_str());
      std::string timing_type =
          clock_ports.contains(start_vertex)
              ? _port_to_logic_clkpoint.values(start_vertex)
                        .front()
                        ->isRisingTriggered()  // TODO(to taosimin), may be
                                               // more than one clk pin.
                    ? "rising_edge"
                    : "falling_edge"
              : "combinational";
      lib_arc->set_timing_type(timing_type.c_str());

      const char* timing_sense =
          (trans == path_data.top()->get_trans_type())
              ? "positive_unate"
              : "negative_unate";  // TODO(to taosimin), non-unate should
                                   // consider.
      lib_arc->set_timing_sense(timing_sense);

      // delay table
      {
        auto delay_table_value = std::make_unique<LibFloatValue>(
            FS_TO_NS(delay_data->get_arrive_time()));

        auto delay_table_type = delay_data->get_trans_type() == TransType::kRise
                                    ? LibTable::TableType::kCellRise
                                    : LibTable::TableType::kCellFall;
        auto lib_delay_table = std::make_unique<LibTable>(
            delay_table_type, nullptr);  // TODO(to taosimin), construct the
                                         // table template, timing sense
        lib_delay_table->addTableValue(std::move(delay_table_value));

        delay_model->addTable(std::move(lib_delay_table));
      }

      // slew table
      {
        auto slew_table_value =
            std::make_unique<LibFloatValue>(FS_TO_NS(slew_data->get_slew()));

        auto slew_table_type = slew_data->get_trans_type() == TransType::kRise
                                   ? LibTable::TableType::kRiseTransition
                                   : LibTable::TableType::kFallTransition;
        auto lib_slew_table = std::make_unique<LibTable>(
            slew_table_type, nullptr);  // TODO(to taosimin), construct the
                                        // table template, timing sense
        lib_slew_table->addTableValue(std::move(slew_table_value));

        delay_model->addTable(std::move(lib_slew_table));
      }
    }

    lib_arc->set_table_model(std::move(delay_model));
    lib_arc->set_owner_cell(design_timing_cell.get());
    design_timing_cell->addLibertyArc(std::move(lib_arc));
  };

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();
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
  return 1;
}

}  // namespace ista