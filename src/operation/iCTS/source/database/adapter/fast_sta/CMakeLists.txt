add_library(
  icts_source_database_adapter_fast_sta
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaAdapter.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaBuilder.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaChar.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaClockTree.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaDmpCeff.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaIncremental.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaLiberty.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaParasitics.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaPower.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaReport.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaTiming.cc
  ${ICTS_DATABASE_ADAPTER_FAST_STA}/FastStaTypes.cc
)

target_link_libraries(
  icts_source_database_adapter_fast_sta
  PRIVATE
  icts_source_database_adapter_sta
  icts_source_database_config
  icts_source_database_design
  icts_source_database_io
  icts_source_database_routing
  icts_source_utils
  icts_source_utils_logger
  absl::fixed_array
)

target_include_directories(
  icts_source_database_adapter_fast_sta
  PUBLIC
  ${ICTS_DATABASE_ADAPTER_FAST_STA}
  ${ICTS_DATABASE_ADAPTER}
  PRIVATE
  ${ICTS_DATABASE}
  ${ICTS_UTILS}
  ${HOME_DATABASE}/manager/parser
)

if(DEBUG_ICTS_SOURCE_DATABASE_ADAPTER_FAST_STA)
  if(MSVC)
    target_compile_options(icts_source_database_adapter_fast_sta PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_database_adapter_fast_sta PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_database_adapter_fast_sta PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
