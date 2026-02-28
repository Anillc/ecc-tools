set(ICTS_MODULE_CHAR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilder.cc
)

add_library(icts_source_module_characterization ${ICTS_MODULE_CHAR_SRC})

target_link_libraries(
  icts_source_module_characterization
  PUBLIC
  icts_source_database_characterization
)

target_include_directories(icts_source_module_characterization PUBLIC ${ICTS_SOURCE} ${ICTS_API})

if(DEBUG_ICTS_SOURCE_MODULE_CHARACTERIZATION)
  if(MSVC)
    target_compile_options(icts_source_module_characterization PUBLIC /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module_characterization PUBLIC -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
