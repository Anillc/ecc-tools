add_library(icts_test_common_realtech_facade INTERFACE)

target_link_libraries(
  icts_test_common_realtech_facade
  INTERFACE
  icts_test_common_realtech_support
)
