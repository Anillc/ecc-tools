set(ICTS_DATA_MANAGER ${ICTS_SOURCE}/data_manager)
set(ICTS_MODULE ${ICTS_SOURCE}/module)
set(ICTS_UTILS ${ICTS_SOURCE}/utils)

# build
add_subdirectory(${ICTS_DATA_MANAGER})
add_subdirectory(${ICTS_UTILS})
add_subdirectory(${ICTS_MODULE})

add_library(icts_source INTERFACE)

target_link_libraries(icts_source INTERFACE icts_data_manager icts_module
                                            icts_utils)
