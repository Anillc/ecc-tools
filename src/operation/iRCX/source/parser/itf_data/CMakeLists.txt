cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)

set( IRC_ITF_DB
  Layer.cpp
  ProcessCorner.cpp
  VariationParams.cpp
)  
add_library(itf_data ${IRC_ITF_DB})
target_link_libraries(itf_data
 PUBLIC
  itf
)