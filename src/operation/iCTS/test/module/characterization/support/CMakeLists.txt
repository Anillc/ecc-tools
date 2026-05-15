add_library(
  icts_test_module_characterization_support
  STATIC
  ${ICTS_TEST}/module/characterization/support/CharacterizationRealTechFrontierSupport.cc
  ${ICTS_TEST}/module/characterization/support/CharacterizationRealTechTestSupport.cc
)

target_link_libraries(
  icts_test_module_characterization_support
  PUBLIC
    icts_test_base
  PRIVATE
    icts_test_common_io
    icts_test_common_logging_support
    icts_test_common_realtech_support
)
