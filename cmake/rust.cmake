macro(SETUP_RUST_PROJECT)
if(NOT DEFINED RUST_BUILD_ROOT)
    set(RUST_BUILD_ROOT ${CMAKE_CURRENT_BINARY_DIR}/${RUST_PROJECT_NAME}-rust)
endif()

set(RUST_TARGET_DIR ${RUST_BUILD_ROOT}/target)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(RUST_LIB_PATH ${RUST_TARGET_DIR}/debug/lib${RUST_PROJECT_NAME}.${RUST_LIB_TYPE})
    set(RUST_BUILD_CMD_OPTION "")
else()
    set(RUST_LIB_PATH ${RUST_TARGET_DIR}/release/lib${RUST_PROJECT_NAME}.${RUST_LIB_TYPE})
    set(RUST_BUILD_CMD_OPTION "--release")
endif()
endmacro()

macro(ADD_EXTERNAL_PROJ proj_name)

include(ExternalProject)

if(NOT DEFINED RUST_TARGET_DIR)
    SETUP_RUST_PROJECT()
endif()

ExternalProject_Add(
    ${RUST_PROJECT_NAME}
    SOURCE_DIR ${RUST_PROJECT_DIR}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        CARGO_TARGET_DIR=${RUST_TARGET_DIR}
        cargo build ${RUST_BUILD_CMD_OPTION}
    INSTALL_COMMAND ""
    BUILD_ALWAYS 1
    BUILD_BYPRODUCTS ${RUST_LIB_PATH}
)

add_dependencies(${proj_name} ${RUST_PROJECT_NAME})

endmacro()

include_directories(${HOME_DATABASE}/manager/parser/rust-common)
