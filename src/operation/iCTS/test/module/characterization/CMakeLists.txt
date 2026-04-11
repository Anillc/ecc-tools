add_subdirectory(support)

icts_add_test_executable(
  icts_test_module_characterization
  SOURCES
    ${ICTS_TEST}/module/characterization/SegmentJoinTest.cc
    ${ICTS_TEST}/module/characterization/HTreeJoinTest.cc
    ${ICTS_TEST}/module/characterization/PrunerTest.cc
    ${ICTS_TEST}/module/characterization/PatternHashTest.cc
  LIBS
    icts_test_module_characterization_support
)
