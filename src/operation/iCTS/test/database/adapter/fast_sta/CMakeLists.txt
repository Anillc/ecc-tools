icts_add_test_executable(
  icts_test_database_adapter_fast_sta
  SOURCES
    ${ICTS_TEST}/database/adapter/fast_sta/FastSTATest.cc
  LIBS
    icts_source_database_adapter_fast_sta
)
