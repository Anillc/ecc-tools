icts_add_test_executable(
  icts_test_flow_synthesis
  SOURCES
    ${ICTS_TEST}/flow/synthesis/ClockSynthesisTest.cc
)

icts_add_test_executable(
  icts_test_flow_synthesis_realtech
  REALTECH
  SOURCES
    ${ICTS_TEST}/flow/synthesis/ClockSynthesisVisualizationSupport.cc
    ${ICTS_TEST}/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc
  LIBS
    icts_test_common_io
    icts_test_common_logging_support
    icts_test_common_realtech_support
    icts_test_module_characterization_support
)
