# math
if(DEBUG_ICTS_MATH)
  message(STATUS "CTS: DEBUG_ICTS_MATH")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_MATH")
  set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_STANDARD 17)
add_library(icts_math ${ICTS_UTILS}/math/ModelFactory.cc)

find_package(Eigen3 QUIET REQUIRED)
message(STATUS "CTS: Eigen3 ${EIGEN3_INCLUDE_DIR}")

target_link_libraries(icts_math PUBLIC Eigen3::Eigen)
target_include_directories(icts_math PUBLIC ${ICTS_UTILS}/math)
