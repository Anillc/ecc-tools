if(DEBUG_ICTS_TEST)
  message(STATUS "CTS: DEBUG_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Release")
endif()
# Set(GTest_DIR "/usr/src/googletest/googletest")

# TreeBuilderTest
# add_executable(icts_tree_builder_test ${ICTS_TEST}/TreeBuilderTest.cc)
# target_link_libraries(
#   icts_tree_builder_test PUBLIC icts_source icts_api icts_test_external_libs
#   ) # ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY}
# target_include_directories(icts_tree_builder_test PUBLIC ${ICTS_TEST})
find_package(GTest REQUIRED)
# AnnealOptTest
add_executable(icts_anneal_opt_test ${ICTS_TEST}/AnnealOptTest.cc)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
target_link_libraries(
  icts_anneal_opt_test PUBLIC icts_source icts_api icts_test_external_libs
  gtestd gtest_maind
  -u_ZN3ipl17SteinerWirelength25updateAllNetWorkPointPairEv
  -u_ZN3ipl13NesterovPlace21printNesterovDatabaseEv
  -u_ZN3ito3iTO5runTOEv
  -u_ZN3idm11DataManager7saveDefENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
  -u_ZN4icts9GDSPloter10plotDesignERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE

  )
else()
target_link_libraries(
  icts_anneal_opt_test PUBLIC icts_source icts_api icts_test_external_libs
  gtest gtest_main
  -u_ZN3ipl17SteinerWirelength25updateAllNetWorkPointPairEv
  -u_ZN3ipl13NesterovPlace21printNesterovDatabaseEv
  -u_ZN3ito3iTO5runTOEv
  -u_ZN3idm11DataManager7saveDefENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
  -u_ZN4icts9GDSPloter10plotDesignERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE

  ) # ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY}
endif()
target_include_directories(icts_anneal_opt_test PUBLIC ${ICTS_TEST})
