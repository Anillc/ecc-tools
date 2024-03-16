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

#include "StaFunc.hh"
#include "StaGraph.hh"
#include "liberty/Liberty.hh"
#include "BTreeMap.hh"

#include <vector>
#include <map>

namespace ista {

/**
 * @brief extract the timing model of the design.
 *
 */
class StaCharacterTiming : public StaFunc {
 public:
  enum CharacterState {
    kCollectEndpoints,
    kPropagateSlewAndDelay,
    kPropagateATFromPort,
    kBackPropagateRTToPort,
    kGenTimingModel
  };
  unsigned operator()(StaGraph* the_graph) override;
  unsigned operator()(StaVertex* the_vertex) override;
  unsigned operator()(StaArc* the_arc) override;

 private:
  unsigned collectInterfaceLogicEndPoint(StaGraph* the_graph);
  unsigned propagateSlewAndDelay(StaGraph* the_graph);
  unsigned propagateATFromPort(StaGraph* the_graph);
  unsigned backPropagateRTToPort(StaGraph* the_graph);
  unsigned genTimingModel(StaGraph* the_graph, const char* model_path);

  std::unique_ptr<LibertyLibrary> _design_timing_model; //!< The design timing model as lib format.

  std::vector<StaVertex*> _interface_logic_endpoints; //!< The collected design interface logic sequential endpoint.
  ieda::Multimap<StaVertex*, StaVertex*> _port_to_logic_endpoint; //!< The map from port to logic endpoint.
  ieda::Multimap<StaVertex*, StaVertex*> _output_port_to_input_port; //!< The map from input port to output port.
  ieda::Multimap<StaVertex*, StaVertex*> _logic_clkpoint_to_port; //!< The map from logic clkpoint to port.
  ieda::Multimap<StaVertex*, StaVertex*> _port_to_logic_clkpoint; //!< The map from port to logic clkpoint.

  CharacterState _state = kCollectEndpoints; // as the timing model compose of many points, we need to track the state.
  StaVertex* _current_port_vertex = nullptr; // track the current port vertex.
};

}  // namespace ista
