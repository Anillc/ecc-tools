add_library(workspace
    workspace.cpp
)

target_link_libraries(workspace
    PUBLIC
        str
)

target_include_directories(workspace
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_THIRDPARTY}/json
)
