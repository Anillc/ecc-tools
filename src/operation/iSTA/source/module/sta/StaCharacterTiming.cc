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
    if (the_vertex->is_end()) {
      if (_state == kCollectEndpoints) {
        // collect the interface logic endpoint.
        _interface_logic_endpoints.emplace_back(the_vertex);
      } else if (_state == kBackPropagateRTToPort) {
        // set the constrain require time.
      }

      return 1;
    }

    // fwd
    FOREACH_SRC_ARC(the_vertex, src_arc) {
      (*this)(src_arc);
      (*this)(src_arc->get_snk());
    }

  } else if (_state == kPropagateSlewAndDelay ||
             _state == kPropagateATFromPort) {
    if (the_vertex->is_port()) {
      // set init slew or AT.
    }

    // bwd
    FOREACH_SNK_ARC(the_vertex, snk_arc) {
      // compute the slew and delay along the arc.
      (*this)(snk_arc);
      (*this)(snk_arc->get_src());
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
  if (_state == kPropagateSlewAndDelay) {
    // propagate the slew and delay along the arc.
    StaSlewPropagation slew_propagation;
    slew_propagation(the_arc);

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

  // then propagate the slew delay from the port to the first sequential cell.
  propagateSlewAndDelay(the_graph);

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
  FOREACH_PORT_VERTEX(the_graph, port_vertex) { (*this)(port_vertex); }

  return 1;
}

/**
 * @brief propagate slew and delay from the port to the first sequential cell
 * endpoint.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateSlewAndDelay(StaGraph* the_graph) {
  _state = kPropagateSlewAndDelay;
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
 * @brief propagate RT from endpoint to port.
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

  _design_timing_model = std::make_unique<LibertyLibrary>(model_name.c_str());
  auto design_timing_cell = std::make_unique<LibertyCell>(the_graph->get_nl()->get_name(), _design_timing_model.get());

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();
    if (the_port->isInput()) {
      // construct the constrain arc, need know the constrained arc, and the constrain port.
    } else if (the_port->isOutput()) {
      // construct the delay arc.
    }
  }

  _design_timing_model->addLibertyCell(std::move(design_timing_cell));
  return 1;
}

}  // namespace ista