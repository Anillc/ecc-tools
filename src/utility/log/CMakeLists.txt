CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
SET (CMAKE_CXX_STANDARD 20)

AUX_SOURCE_DIRECTORY(./ SRC)

SET(LINK_glog   "glog")
SET(LINK_gflags "gflags")
SET(LINK_unwind "unwind")

# TODO:
# FIXME:
if(BUILD_STATIC_LIB)
  ADD_LIBRARY(log ${SRC})
  # SET(LINK_glog   "libglog")
  # SET(LINK_gflags "libgflags")
else()
  ADD_LIBRARY(log SHARED ${SRC})
endif()

TARGET_LINK_LIBRARIES(log PUBLIC ${LINK_glog} ${LINK_gflags} ${LINK_unwind})
# install(TARGETS log DESTINATION ${CMAKE_INSTALL_PREFIX})