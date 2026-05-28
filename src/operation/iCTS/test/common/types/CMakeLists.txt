add_library(
  icts_test_common_types
  STATIC
  ${ICTS_TEST}/common/types/TestDataTypes.cc
)

target_link_libraries(
  icts_test_common_types
  PUBLIC
  icts_test_base
)
