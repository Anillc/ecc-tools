add_subdirectory(support)

icts_add_test_executable(
  icts_test_module_topology_gen
  SOURCES
    ${ICTS_TEST}/module/topology/topology_gen/TopologyGenTest.cc
  LIBS
    icts_test_module_topology_gen_support
)
