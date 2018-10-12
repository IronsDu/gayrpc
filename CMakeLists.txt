cmake_minimum_required (VERSION 2.6)
project(gayrpc)

include_directories("${PROJECT_SOURCE_DIR}/include/")
include_directories("${PROJECT_SOURCE_DIR}/utils/")

if(UNIX)
find_library(BRYNET_LIBRARY brynet)
find_package(protobuf REQUIRED)
endif()

if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
elseif(UNIX)
  if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  endif()
endif()

add_subdirectory(examples/echo)
add_subdirectory(examples/benchmark)
add_subdirectory(examples/raft)
