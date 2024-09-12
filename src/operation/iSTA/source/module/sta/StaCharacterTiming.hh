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
 * @file StaCharacterTiming.hh
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief extract the timing model of design.
 * @version 0.1
 * @date 2024-03-12
 *
 */
#pragma once

#include <map>
#include <vector>

#include "BTreeMap.hh"
#include "StaFunc.hh"
#include "StaGraph.hh"
#include "liberty/Lib.hh"

namespace ista {

/**
 * @brief extract the timing model of the design.
 *
 */
class StaCharacterTiming : public StaFunc {
 public:
  enum CharacterState {
    kCollectEndpoints,
    kPropagateSlew,
    kPropagateDelay,
    kPropagateATFromPort,
    kBackPropagateRTToPort,
    kGenTimingModel
  };

  StaCharacterTiming(AnalysisMode analysis_mode, const char* model_path)
      : _model_path(model_path), _analysis_mode(analysis_mode) {}
  ~StaCharacterTiming() = default;

  unsigned operator()(StaGraph* the_graph) override;
  unsigned operator()(StaVertex* the_vertex) override;
  unsigned operator()(StaArc* the_arc) override;
  LibLibrary* get_design_timing_model() { return _design_timing_model.get(); }

 private:
  unsigned init(StaGraph* the_graph);
  unsigned collectInterfaceLogicEndPoint(StaGraph* the_graph);
  unsigned checkAndBreakLoop(StaGraph* the_graph);
  unsigned propagateSlew(StaGraph* the_graph);
  unsigned propagateDelay(StaGraph* the_graph);
  unsigned propagateATFromPort(StaGraph* the_graph);
  unsigned backPropagateRTToPort(StaGraph* the_graph);
  unsigned genTimingModel(StaGraph* the_graph, const char* model_path);

  std::unique_ptr<LibLibrary>
      _design_timing_model;     //!< The design timing model as lib format.
  std::string _model_path;      //!< The design timing model path.
  AnalysisMode _analysis_mode;  //!< The analysis mode.

  std::set<StaVertex*>
      _interface_logic_endpoints;  //!< The collected design interface logic
                                   //!< sequential endpoint and output port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _port_to_logic_endpoint;  //!< The map from port to logic endpoint.
  ieda::Multimap<StaVertex*, StaVertex*>
      _output_port_to_input_port;  //!< The map from input port to output port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _logic_clkpoint_to_port;  //!< The map from logic clkpoint to port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _port_to_logic_clkpoint;  //!< The map from port to logic clkpoint.

  CharacterState _state =
      kCollectEndpoints;  // as the timing model compose of many points, we need
                          // to track the state.
  StaVertex* _current_port_vertex = nullptr;  // track the current port vertex.
};

}  // namespace ista
