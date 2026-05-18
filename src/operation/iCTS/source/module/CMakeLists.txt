set(ICTS_MODULE_BUFFERING ${ICTS_MODULE}/buffering)
set(ICTS_MODULE_DRV ${ICTS_MODULE}/drv)
set(ICTS_MODULE_REPORT ${ICTS_MODULE}/report)
set(ICTS_MODULE_ROUTING ${ICTS_MODULE}/routing)
set(ICTS_MODULE_TIMING ${ICTS_MODULE}/timing)
set(ICTS_MODULE_TOPOLOGY ${ICTS_MODULE}/topology)
set(ICTS_MODULE_CHARACTERIZATION ${ICTS_MODULE}/characterization)
set(ICTS_MODULE_ANALYTICAL_CHARACTERIZATION ${ICTS_MODULE}/analytical_characterization)

# add_subdirectory(${ICTS_MODULE_BUFFERING})
# add_subdirectory(${ICTS_MODULE_DRV})
# add_subdirectory(${ICTS_MODULE_REPORT})
add_subdirectory(${ICTS_MODULE_CHARACTERIZATION})
add_subdirectory(${ICTS_MODULE_ROUTING})
add_subdirectory(${ICTS_MODULE_TIMING})
add_subdirectory(${ICTS_MODULE_TOPOLOGY})
add_subdirectory(${ICTS_MODULE_ANALYTICAL_CHARACTERIZATION})

add_library(icts_source_module INTERFACE)

target_link_libraries(
  icts_source_module
  INTERFACE
  # icts_source_module_buffering
  icts_source_database
  # icts_source_module_drv
  # icts_source_module_report
  icts_source_module_routing
  icts_source_module_timing
  icts_source_module_topology
  icts_source_module_characterization
)

target_include_directories(icts_source_module INTERFACE ${ICTS_MODULE})

if(DEBUG_ICTS_SOURCE_MODULE)
  if(MSVC)
    target_compile_options(icts_source_module INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
