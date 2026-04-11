add_library(
  icts_test_common_realtech_support
  STATIC
  ${ICTS_TEST}/common/realtech/support/RealTechSetupSupport.cc
)

target_link_libraries(
  icts_test_common_realtech_support
  PUBLIC
  icts_test_common_types
  PRIVATE
  icts_test_common_realtech_asset_loader
  icts_test_common_realtech_load_factory
)
