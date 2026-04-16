add_subdirectory(support)

icts_add_test_executable(
  icts_test_module_characterization
  SOURCES
    ${ICTS_TEST}/module/characterization/BufferingPatternTest.cc
    ${ICTS_TEST}/module/characterization/SegmentJoinTest.cc
    ${ICTS_TEST}/module/characterization/HTreeJoinTest.cc
    ${ICTS_TEST}/module/characterization/PrunerTest.cc
  LIBS
    icts_test_module_characterization_support
)

icts_add_test_executable(
  icts_test_module_characterization_realtech
  REALTECH
  SOURCES
    ${ICTS_TEST}/module/characterization/CharacterizationRealTechSmokeTest.cc
    ${ICTS_TEST}/module/characterization/CharacterizationRealTechExactRegressionTest.cc
    ${ICTS_TEST}/module/characterization/CharacterizationRealTechFallbackTest.cc
  LIBS
    icts_test_module_characterization_support
    icts_test_common_realtech_support
    icts_test_common_io
)
