add_library(icts_source_database_design INTERFACE)

target_link_libraries(
  icts_source_database_design
  INTERFACE
  icts_source_database_spatial
  icts_source_database_config
)

target_include_directories(icts_source_database_design INTERFACE ${ICTS_DATABASE_DESIGN} ${ICTS_DATABASE})

if(DEBUG_ICTS_SOURCE_DATABASE_DESIGN)
  if(MSVC)
    target_compile_options(icts_source_database_design INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_database_design INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
