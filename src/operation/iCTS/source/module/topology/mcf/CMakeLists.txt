add_library(icts_source_module_topology_mcf INTERFACE)

target_link_libraries(icts_source_module_topology_mcf INTERFACE lemon)

target_include_directories(
  icts_source_module_topology_mcf
  INTERFACE
  ${ICTS_SOLVER}/tools/balance_clustering/min_cost_flow
)

if(DEBUG_ICTS_SOURCE_MODULE_TOPOLOGY_MCF)
  if(MSVC)
    target_compile_options(icts_source_module_topology_mcf INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module_topology_mcf INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
