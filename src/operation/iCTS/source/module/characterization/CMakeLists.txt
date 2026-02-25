add_library(icts_source_module_characterization INTERFACE)

target_link_libraries(
  icts_source_module_characterization
  INTERFACE
  icts_source_database_characterization
)

target_include_directories(icts_source_module_characterization INTERFACE ${ICTS_SOURCE})

if(DEBUG_ICTS_SOURCE_MODULE_CHARACTERIZATION)
  if(MSVC)
    target_compile_options(icts_source_module_characterization INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module_characterization INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
