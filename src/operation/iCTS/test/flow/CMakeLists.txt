icts_add_test_executable(
  icts_test_flow
  SOURCES
    ${ICTS_TEST}/flow/FlowTest.cc
  LIBS
    idm
)

target_include_directories(
  icts_test_flow
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src/feature/database
)

add_subdirectory(synthesis)
