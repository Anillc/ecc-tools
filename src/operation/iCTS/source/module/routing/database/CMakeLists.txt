add_library(icts_source_module_routing_database INTERFACE)

target_link_libraries(
  icts_source_module_routing_database
  INTERFACE
  icts_source_database_spatial
)

target_include_directories(
  icts_source_module_routing_database
  INTERFACE
  ${ICTS_MODULE_ROUTING}/database
)
