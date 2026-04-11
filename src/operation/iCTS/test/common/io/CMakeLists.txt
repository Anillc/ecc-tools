add_library(
  icts_test_common_io
  STATIC
  ${ICTS_TEST}/common/io/TestArtifactIO.cc
)

target_link_libraries(
  icts_test_common_io
  PRIVATE
  icts_test_common_types
)
