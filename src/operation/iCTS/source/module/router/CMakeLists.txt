if(DEBUG_ICTS_ROUTER)
  message(STATUS "CTS: DEBUG_ICTS_ROUTER")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_ROUTER")
  set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(icts_router ${ICTS_MODULE}/router/Router.cc)

target_link_libraries(icts_router PRIVATE icts_committer
                                          icts_synthesis_ir
                                          icts_data_manager
                                          icts_session
                                          icts_synthesis)

target_include_directories(icts_router PUBLIC ${ICTS_MODULE}/router)
