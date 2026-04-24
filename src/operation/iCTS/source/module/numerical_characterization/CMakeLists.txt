set(ICTS_MODULE_NUMERICAL_CHARACTERIZATION_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/NumericalSample.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Polynomial2D.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/PatternResponseModel.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/NumericalCharLibrary.cc
)

add_library(icts_source_module_numerical_characterization ${ICTS_MODULE_NUMERICAL_CHARACTERIZATION_SRC})

target_link_libraries(
  icts_source_module_numerical_characterization
  PUBLIC
  icts_source_database_characterization
)

target_include_directories(
  icts_source_module_numerical_characterization
  PUBLIC
  ${ICTS_MODULE}
)

if(DEBUG_ICTS_SOURCE_MODULE_NUMERICAL_CHARACTERIZATION)
  if(MSVC)
    target_compile_options(icts_source_module_numerical_characterization PRIVATE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module_numerical_characterization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
