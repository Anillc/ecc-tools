add_executable(
    icts_test
    ${ICTS_TEST}/main.cc
    ${ICTS_TEST}/common/TestUtils.cc
    ${ICTS_TEST}/module/topology/TopologyGenTest.cc
    ${ICTS_TEST}/module/characterization/CharacterizationTest.cc
)

target_include_directories(icts_test PRIVATE ${ICTS_TEST})

target_link_libraries(
    icts_test
    PRIVATE
    icts_source icts_api icts_test_external_libs
)
