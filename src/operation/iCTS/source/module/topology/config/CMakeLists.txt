add_library(icts_source_module_topology_config INTERFACE)

target_include_directories(
  icts_source_module_topology_config
  INTERFACE
  ${ICTS_MODULE_TOPOLOGY}/config
)
