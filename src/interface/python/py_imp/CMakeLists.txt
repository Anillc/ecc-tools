aux_source_directory(. PY_IMP_SRC)
aux_source_directory(./idb_to_imp_db/ PY_IMPDB_SRC)
include_directories(./idb_to_imp_db/)
include_directories(./)

add_library(py_imp ${PY_IMP_SRC} 
${PY_IMPDB_SRC}
)

target_link_libraries(py_imp
    PUBLIC
    imp_api
    idm
    str
    pybind11::pybind11
)

target_include_directories(py_imp
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)