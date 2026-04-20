cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)

set(IRC_ITF
  ItfBuilder.cpp
  ItfRead.cpp
)
add_library(itf_builder ${IRC_ITF})
target_link_libraries(itf_builder
 PUBLIC
  itf_data itf
)