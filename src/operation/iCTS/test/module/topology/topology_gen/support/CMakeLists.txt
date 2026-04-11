add_subdirectory(case)
add_subdirectory(analysis)
add_subdirectory(artifact)
add_subdirectory(core)

add_library(icts_test_module_topology_gen_support INTERFACE)

target_link_libraries(
  icts_test_module_topology_gen_support
  INTERFACE
  icts_test_module_topology_gen_core_support
)
