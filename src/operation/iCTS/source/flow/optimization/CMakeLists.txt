add_library(
  icts_source_flow_optimization
  ${ICTS_FLOW_OPTIMIZATION}/Optimization.cc
)

target_link_libraries(
  icts_source_flow_optimization
  PRIVATE
  icts_source_database
  icts_source_database_adapter_fast_sta
  icts_source_database_design
  icts_source_flow_synthesis_htree_characterization_library
  icts_source_module_routing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_optimization
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_OPTIMIZATION)
  if(MSVC)
    target_compile_options(icts_source_flow_optimization PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_optimization PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_optimization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
