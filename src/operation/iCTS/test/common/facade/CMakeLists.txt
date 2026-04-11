add_library(
  icts_test_common_facade
  STATIC
  ${ICTS_TEST}/common/TestUtils.cc
  ${ICTS_TEST}/common/RealTechSetup.cc
)

target_link_libraries(
  icts_test_common_facade
  PRIVATE
  icts_test_common_data
  icts_test_common_io
  icts_test_common_topology_support
  icts_test_common_visualization
  icts_test_common_realtech_support
)
