set(ICTS_LOGGER ${ICTS_UTILS}/logger)
set(ICTS_GEOMETRY ${ICTS_UTILS}/geometry)

add_subdirectory(${ICTS_LOGGER})
add_subdirectory(${ICTS_GEOMETRY})

add_library(icts_source_utils INTERFACE)

target_link_libraries(
  icts_source_utils
  INTERFACE
  log
  icts_source_utils_logger
  icts_source_utils_geometry
)

target_include_directories(
  icts_source_utils
  INTERFACE
  ${ICTS_UTILS}
  ${ICTS_LOGGER}
  ${ICTS_GEOMETRY}
)

if(DEBUG_ICTS_SOURCE_UTILS)
  if(MSVC)
    target_compile_options(icts_source_utils INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_utils INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
