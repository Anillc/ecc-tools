add_library(
  icts_test_common_realtech_asset_loader
  STATIC
  ${ICTS_TEST}/common/realtech/asset/RealTechAssetLoader.cc
)

target_link_libraries(
  icts_test_common_realtech_asset_loader
  PRIVATE
  icts_test_common_types
  icts_test_common_io
  icts_test_realtech_base
)
