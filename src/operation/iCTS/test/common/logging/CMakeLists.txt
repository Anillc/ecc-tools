add_library(
  icts_test_common_logging_support
  STATIC
  ${ICTS_TEST}/common/logging/ScopedLogFile.cc
)

target_link_libraries(
  icts_test_common_logging_support
  PUBLIC
  icts_test_base
)
