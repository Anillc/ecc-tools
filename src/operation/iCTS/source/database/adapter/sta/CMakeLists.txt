add_library(
  icts_source_database_adapter_sta
  ${ICTS_SOURCE}/database/adapter/sta/STAAdapter.cc
  ${ICTS_SOURCE}/database/adapter/sta/cell_master/STAAdapterCellQuery.cc
  ${ICTS_SOURCE}/database/adapter/sta/clock_lookup/STAAdapterClockLookup.cc
  ${ICTS_SOURCE}/database/adapter/sta/net_rc/STAAdapterWireRc.cc
  ${ICTS_SOURCE}/database/adapter/sta/net_rc_tree/STAAdapterRcTree.cc
  ${ICTS_SOURCE}/database/adapter/sta/root_driver/STAAdapterRootDriverQuery.cc
  ${ICTS_SOURCE}/database/adapter/sta/timing_update/STAAdapterTimingUpdate.cc
  ${ICTS_SOURCE}/database/adapter/sta/timing_query/STAAdapterTimingQuery.cc
)

target_include_directories(
  icts_source_database_adapter_sta
  PUBLIC
  ${ICTS_DATABASE_ADAPTER_STA}
  PRIVATE
  ${ICTS_DATABASE_ADAPTER_STA}/timing_query
  ${ICTS_DATABASE}
  ${ICTS_UTILS}
  ${HOME_DATABASE}/data/design
  ${HOME_DATABASE}/data/design/db_design
  ${HOME_DATABASE}/data/design/db_layout
)

target_link_libraries(
  icts_source_database_adapter_sta
  PUBLIC
  icts_source_utils_logger
  PRIVATE
  icts_source_database_design
  icts_source_database_config
  icts_source_database_routing
  icts_source_utils
  power
  icts_api_external_libs
)

if(DEBUG_ICTS_SOURCE_DATABASE_ADAPTER_STA)
  if(MSVC)
    target_compile_options(icts_source_database_adapter_sta PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_database_adapter_sta PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_database_adapter_sta PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
