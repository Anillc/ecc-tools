if(DEBUG_ICTS_SYNTHESIS_IR)
  message(STATUS "CTS: DEBUG_ICTS_SYNTHESIS_IR")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_SYNTHESIS_IR")
  set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(
  icts_synthesis_ir
  ${ICTS_UTILS}/synthesis_ir/Pin.cc ${ICTS_UTILS}/synthesis_ir/Inst.cc
  ${ICTS_UTILS}/synthesis_ir/Net.cc)

target_link_libraries(icts_synthesis_ir PUBLIC icts_database
                                         PRIVATE icts_runtime)

target_include_directories(icts_synthesis_ir PUBLIC ${ICTS_UTILS}/synthesis_ir)
