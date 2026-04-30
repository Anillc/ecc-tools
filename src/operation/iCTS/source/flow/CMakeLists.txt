set(ICTS_FLOW_CLOCK_TREE_VIEW ${ICTS_FLOW}/clock_tree_view)
set(ICTS_FLOW_EVALUATION ${ICTS_FLOW}/evaluation)
set(ICTS_FLOW_HTREE ${ICTS_FLOW}/htree)
set(ICTS_FLOW_NETLIST ${ICTS_FLOW}/netlist)
set(ICTS_FLOW_VISUALIZATION ${ICTS_FLOW}/visualization)
set(ICTS_FLOW_RUN_SETUP ${ICTS_FLOW}/run_setup)
set(ICTS_FLOW_STAGE ${ICTS_FLOW}/stage)
set(ICTS_FLOW_SYNTHESIS ${ICTS_FLOW}/synthesis)

add_subdirectory(${ICTS_FLOW_EVALUATION})
add_subdirectory(${ICTS_FLOW_HTREE})
add_subdirectory(${ICTS_FLOW_NETLIST})
add_subdirectory(${ICTS_FLOW_RUN_SETUP})
add_subdirectory(${ICTS_FLOW_SYNTHESIS})
add_subdirectory(${ICTS_FLOW_CLOCK_TREE_VIEW})
add_subdirectory(${ICTS_FLOW_VISUALIZATION})
add_subdirectory(${ICTS_FLOW_STAGE})

add_library(icts_source_flow ${ICTS_FLOW}/FlowManager.cc)

target_link_libraries(
  icts_source_flow
  PUBLIC
  icts_source_flow_evaluation
  icts_source_flow_stage
  PRIVATE
  icts_source_database
  icts_source_module
  icts_source_flow_netlist
  icts_source_flow_visualization
  icts_source_flow_run_setup
  icts_source_utils
)

target_include_directories(
  icts_source_flow
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW)
  if(MSVC)
    target_compile_options(icts_source_flow PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
