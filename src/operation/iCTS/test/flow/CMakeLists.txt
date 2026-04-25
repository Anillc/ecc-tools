icts_add_test_executable(
  icts_test_flow_manager
  SOURCES
    ${ICTS_TEST}/flow/FlowManagerTest.cc
)

add_subdirectory(htree)
add_subdirectory(synthesis)
