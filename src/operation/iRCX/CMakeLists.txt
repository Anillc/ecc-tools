cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE "Debug")
# set(CMAKE_BUILD_TYPE "Release")
# conan_basic_setup()

if(TEST_SHELL)
  add_definitions(-DTEST_SHELL)
endif(TEST_SHELL)

# ADD_COMPILE_OPTIONS("-fsanitize=address") LINK_LIBRARIES("-fsanitize=address")

##-------------------------- Global Include Directories ------------------------##
# Set up common include paths for all iRCX modules
# This allows all source files to use direct includes without relative paths

# Utility libraries
include_directories(${HOME_UTILITY})
include_directories(${HOME_UTILITY}/log)
include_directories(${HOME_UTILITY}/stdBase)
include_directories(${HOME_UTILITY}/stdBase/include)

# Database and platform
include_directories(${HOME_DATABASE}/manager/parser)
include_directories(${HOME_PLATFORM}/data_manager)

# Third-party libraries
include_directories(SYSTEM ${HOME_THIRDPARTY})
include_directories(${HOME_THIRDPARTY}/onnxruntime/include)
include_directories(${HOME_THIRDPARTY}/yaml-cpp/include)

# iRCX module directories - organized by functionality
include_directories(${HOME_OPERATION}/iRCX)
include_directories(${HOME_OPERATION}/iRCX/api)
include_directories(${HOME_OPERATION}/iRCX/source/module)
include_directories(${HOME_OPERATION}/iRCX/source/module/database)
include_directories(${HOME_OPERATION}/iRCX/source/module/environment)
include_directories(${HOME_OPERATION}/iRCX/source/module/extract)
include_directories(${HOME_OPERATION}/iRCX/source/module/extract/resistance)
include_directories(${HOME_OPERATION}/iRCX/source/module/extract/resistance)
include_directories(${HOME_OPERATION}/iRCX/source/module/process)
include_directories(${HOME_OPERATION}/iRCX/source/module/shell_cmd)
include_directories(${HOME_OPERATION}/iRCX/source/module/topology)
include_directories(${HOME_OPERATION}/iRCX/source/module/report)
include_directories(${HOME_OPERATION}/iRCX/source/parser)
include_directories(${HOME_OPERATION}/iRCX/source/parser/cap_table)
include_directories(${HOME_OPERATION}/iRCX/source/parser/itf)
include_directories(${HOME_OPERATION}/iRCX/source/parser/itf_builder)
include_directories(${HOME_OPERATION}/iRCX/source/parser/itf_data)
include_directories(${HOME_OPERATION}/iRCX/source/parser/mapping)

include(${HOME_CMAKE}/operation/idb.cmake)

link_directories(${CMAKE_BINARY_DIR}/lib)
link_directories(${HOME_THIRDPARTY}/onnxruntime/)

find_package(Eigen3 REQUIRED)

include_directories(SYSTEM ${EIGEN3_INCLUDE_DIR})
message(STATUS "Eigen3 ${EIGEN3_INCLUDE_DIR}")
link_directories(${EIGEN3_LIBRARY})

add_subdirectory(api)
add_subdirectory(source/module/topology)
add_subdirectory(source/module/environment)
add_subdirectory(source/module/process)
add_subdirectory(source/module/extract)
add_subdirectory(source/module/report)
add_subdirectory(source/module/shell_cmd)
add_subdirectory(source/parser)

add_executable(iRCX main.cc)

target_link_libraries( iRCX
  ircx_engine
  ircx_topology
  ircx_environment
  ircx_process
  rcx_shell_cmd
  # rcx
  log)
