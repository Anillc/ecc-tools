# if(DEBUG_ICTS_API)
#   message(STATUS "CTS: DEBUG_ICTS_API")
#   set(CMAKE_BUILD_TYPE "Debug")
# else()
#   message(STATUS "CTS: RELEASE_ICTS_API")
#   set(CMAKE_BUILD_TYPE "Release")
# endif()

add_library(icts_api ${ICTS_API}/CTSAPI.cc)

target_link_libraries(icts_api PRIVATE icts_data_manager
                                       icts_session
                                       icts_flow
                                       icts_timing_propagator
                                       icts_tree_builder
                                       icts_math
                                       icts_api_external_libs
                                       usage
                                       feature_db)

target_include_directories(icts_api PUBLIC ${ICTS_API}
                                           ${PROJECT_SOURCE_DIR}/src/database)
