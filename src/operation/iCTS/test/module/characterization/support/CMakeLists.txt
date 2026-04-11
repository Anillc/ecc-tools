add_library(icts_test_module_characterization_support INTERFACE)

target_link_libraries(
  icts_test_module_characterization_support
  INTERFACE
  icts_test_base
)
