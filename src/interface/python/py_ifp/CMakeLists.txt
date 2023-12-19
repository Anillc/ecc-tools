aux_source_directory(./idb_to_imp_db/ PY_IFP_SRC)

include_directories(./idb_to_imp_db/)
include_directories(./)

add_library(py_ifp ${PY_IFP_SRC}
                    py_ifp.cpp
                    )

# add_subdirectory(idb_to_imp_db)
target_link_libraries(py_ifp
    PUBLIC
        tool_manager
        idm
        ifp_api
        str
        pybind11::pybind11
)

target_include_directories(py_ifp
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
