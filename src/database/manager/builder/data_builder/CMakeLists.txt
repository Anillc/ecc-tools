add_library(data_process
    header.cpp
    layout_write.cpp
    layout_read.cpp
    design_write.cpp
    design_read.cpp
)

target_include_directories(data_process 
PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
        ${HOME_DATABASE}/basic/geometry
        ${HOME_DATABASE}/manager/service/def_service
)
