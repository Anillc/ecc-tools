add_library(
  icts_test_common_data_distribution
  STATIC
  ${ICTS_TEST}/common/data/distribution/DistributionGenerators.cc
)

target_link_libraries(
  icts_test_common_data_distribution
  PUBLIC
  icts_test_common_types
  PRIVATE
  icts_test_common_data_pin_factory
)
