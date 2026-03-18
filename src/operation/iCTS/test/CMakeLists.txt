add_executable(
  icts_test
  ${ICTS_TEST}/main.cc
  ${ICTS_TEST}/common/TestUtils.cc
  ${ICTS_TEST}/module/topology/TopologyGenTest.cc
  ${ICTS_TEST}/module/characterization/CharacterizationTest.cc
  ${ICTS_TEST}/database/spatial/SpatialRegionTest.cc
  ${ICTS_TEST}/module/routing/LocalLegalizationTest.cc
  ${ICTS_TEST}/module/routing/RouterLegalizationTest.cc
)

target_include_directories(icts_test PRIVATE ${ICTS_TEST})

target_link_libraries(
  icts_test
  PRIVATE
  icts_source
  icts_api
  icts_test_external_libs
)

add_test(NAME icts_test COMMAND icts_test)
