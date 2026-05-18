add_library(
  icts_source_flow_optimization
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationCandidates.cc
  ${ICTS_FLOW_OPTIMIZATION}/Optimization.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationMutation.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationOptions.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationPreparation.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationReport.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationScalableCandidates.cc
  ${ICTS_FLOW_OPTIMIZATION}/OptimizationSolver.cc
)

target_link_libraries(
  icts_source_flow_optimization
  PUBLIC
  icts_source_database_adapter_fast_sta
  icts_source_module_routing
  PRIVATE
  icts_source_database
  icts_source_database_design
  icts_source_flow_synthesis_htree_characterization_library
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
