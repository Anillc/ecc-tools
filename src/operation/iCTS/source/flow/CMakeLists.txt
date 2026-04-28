set(ICTS_FLOW_EVALUATION ${ICTS_FLOW}/evaluation)
set(ICTS_FLOW_HTREE ${ICTS_FLOW}/htree)
set(ICTS_FLOW_NETLIST ${ICTS_FLOW}/netlist)
set(ICTS_FLOW_REPORT ${ICTS_FLOW}/report)
set(ICTS_FLOW_SYNTHESIS ${ICTS_FLOW}/synthesis)

add_subdirectory(${ICTS_FLOW_EVALUATION})
add_subdirectory(${ICTS_FLOW_HTREE})
add_subdirectory(${ICTS_FLOW_NETLIST})
add_subdirectory(${ICTS_FLOW_REPORT})
add_subdirectory(${ICTS_FLOW_SYNTHESIS})

add_library(icts_source_flow ${ICTS_FLOW}/FlowManager.cc)

target_link_libraries(
  icts_source_flow
  PUBLIC
  icts_source_flow_evaluation
  PRIVATE
  icts_source_database
  icts_source_module
  icts_source_flow_htree
  icts_source_flow_netlist
  icts_source_flow_report
  icts_source_flow_synthesis
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
