if(DEBUG_ICTS_TEST)
  message(STATUS "CTS: DEBUG_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_TEST")
  set(CMAKE_BUILD_TYPE "Release")
endif()
# Set(GTest_DIR "/usr/src/googletest/googletest")

# include(FetchContent)
# FetchContent_Declare(
#   googletest
#   # Specify the commit you depend on and update it regularly.
#   URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
# )
# # set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(googletest)
if(PY_MODEL)
  # Python test
  add_executable(icts_py_test ${ICTS_TEST}/PyTest.cc)
  target_link_libraries(icts_py_test PUBLIC icts_source icts_test_external_libs
  libgtest.a libgtest_main.a)
endif()

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
  gtestd gtest_maind)
else()
target_link_libraries(
  icts_anneal_opt_test PUBLIC icts_source icts_api icts_test_external_libs
  gtest gtest_main
  ) # ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY}
endif()
target_include_directories(icts_anneal_opt_test PUBLIC ${ICTS_TEST})
