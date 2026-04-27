icts_add_test_executable(
  icts_test_flow_manager
  SOURCES
    ${ICTS_TEST}/flow/FlowManagerTest.cc
)

target_include_directories(
  icts_test_flow_manager
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src/feature/database
)

add_subdirectory(htree)
add_subdirectory(synthesis)
