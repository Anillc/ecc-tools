if(DEBUG_ICTS_TEST)
  message(STATUS "CTS: DEBUG_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Release")
endif()

# Set(GTest_DIR "/usr/src/googletest/googletest")
find_package(GTest REQUIRED)

# TreeBuilderTest
# add_executable(icts_tree_builder_test ${ICTS_TEST}/TreeBuilderTest.cc)
# target_link_libraries(
#   icts_tree_builder_test PUBLIC icts_source icts_api icts_test_external_libs
# ) # ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY}
# target_include_directories(icts_tree_builder_test PUBLIC ${ICTS_TEST})

# AnnealOptTest
add_executable(icts_anneal_opt_test ${ICTS_TEST}/AnnealOptTest.cc)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(
    icts_anneal_opt_test PUBLIC icts_source icts_api icts_api_external_libs icts_test_external_libs
                                gtestd gtest_maind
                                -u_ZN3ipl17SteinerWirelength25updateAllNetWorkPointPairEv
                                -u_ZN3ipl13NesterovPlace21printNesterovDatabaseEv
                                -u_ZN3ito3iTO5runTOEv
                                -u_ZN3idm11DataManager7saveDefENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
                                -u_ZN4icts9GDSPloter10plotDesignERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE)
else()
  target_link_libraries(
    icts_anneal_opt_test PUBLIC icts_source icts_api icts_api_external_libs icts_test_external_libs
                                gtest gtest_main
                                -u_ZN3ipl17SteinerWirelength25updateAllNetWorkPointPairEv
                                -u_ZN3ipl13NesterovPlace21printNesterovDatabaseEv
                                -u_ZN3ito3iTO5runTOEv
                                -u_ZN3idm11DataManager7saveDefENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
                                -u_ZN4icts9GDSPloter10plotDesignERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE)
endif()
target_include_directories(icts_anneal_opt_test PUBLIC ${ICTS_TEST})

# DesignCommitterTest
add_executable(icts_design_committer_test ${ICTS_TEST}/DesignCommitterTest.cc)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(icts_design_committer_test PUBLIC icts_api icts_api_external_libs
                                                          gtestd gtest_maind)
else()
  target_link_libraries(icts_design_committer_test PUBLIC icts_api icts_api_external_libs
                                                          gtest gtest_main)
endif()
target_include_directories(icts_design_committer_test PUBLIC ${ICTS_TEST})
add_test(NAME icts_design_committer_test COMMAND icts_design_committer_test)

# LongWireBufferingOperatorTest
add_executable(icts_long_wire_buffering_test ${ICTS_TEST}/LongWireBufferingOperatorTest.cc)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(icts_long_wire_buffering_test PUBLIC icts_synthesis_operator
                                                            gtestd gtest_maind)
else()
  target_link_libraries(icts_long_wire_buffering_test PUBLIC icts_synthesis_operator
                                                            gtest gtest_main)
endif()
target_include_directories(icts_long_wire_buffering_test PUBLIC ${ICTS_TEST})
add_test(NAME icts_long_wire_buffering_test COMMAND icts_long_wire_buffering_test)

# TopologyBuilderOperatorTest
add_executable(icts_topology_builder_test ${ICTS_TEST}/TopologyBuilderOperatorTest.cc)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(icts_topology_builder_test PUBLIC icts_synthesis_operator icts_runtime
                                                         gtestd gtest_maind)
else()
  target_link_libraries(icts_topology_builder_test PUBLIC icts_synthesis_operator icts_runtime
                                                         gtest gtest_main)
endif()
target_include_directories(icts_topology_builder_test PUBLIC ${ICTS_TEST})
add_test(NAME icts_topology_builder_test COMMAND icts_topology_builder_test)
