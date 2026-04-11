add_library(icts_test_common_types INTERFACE)

target_link_libraries(
  icts_test_common_types
  INTERFACE
  icts_test_base
)
