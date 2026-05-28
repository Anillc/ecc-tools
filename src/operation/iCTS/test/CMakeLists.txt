include(CMakeParseArguments)

add_library(icts_test_base INTERFACE)

target_include_directories(
  icts_test_base
  INTERFACE
  ${ICTS_TEST}
)

target_link_libraries(
  icts_test_base
  INTERFACE
  log
  icts_source
  icts_api
  icts_test_external_libs
)

add_library(icts_test_realtech_base INTERFACE)

target_link_libraries(
  icts_test_realtech_base
  INTERFACE
  icts_test_base
  idm
)

add_library(icts_test_main OBJECT ${ICTS_TEST}/main.cc)
target_link_libraries(icts_test_main PUBLIC icts_test_base)

option(ICTS_BUILD_SLOW_REALTECH_TESTS "Build slow iCTS real-tech regression tests" OFF)

function(icts_add_test_executable target_name)
  set(options REALTECH)
  set(one_value_args)
  set(multi_value_args SOURCES LIBS)
  cmake_parse_arguments(ICTS_ADD_TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  add_executable(${target_name} ${ICTS_ADD_TEST_SOURCES} $<TARGET_OBJECTS:icts_test_main>)
  target_link_libraries(${target_name} PRIVATE icts_test_base icts_test_common_io ${ICTS_ADD_TEST_LIBS})

  if(ICTS_ADD_TEST_REALTECH)
    target_link_libraries(${target_name} PRIVATE icts_test_realtech_base)
  endif()

  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_subdirectory(common)
add_subdirectory(database)
add_subdirectory(flow)
add_subdirectory(module)
