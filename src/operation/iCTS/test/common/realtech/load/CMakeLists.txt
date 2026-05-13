add_library(
  icts_test_common_realtech_load_factory
  STATIC
  ${ICTS_TEST}/common/realtech/load/RealTechLoadFactory.cc
)

target_link_libraries(
  icts_test_common_realtech_load_factory
  PUBLIC
  icts_test_common_types
  PRIVATE
  icts_test_common_data
  icts_test_realtech_base
)
