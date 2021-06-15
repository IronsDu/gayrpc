cmake_minimum_required (VERSION 2.6)
project(gayrpc)

include_directories("${PROJECT_SOURCE_DIR}/src")
include_directories("${PROJECT_SOURCE_DIR}/dep/ananas/include")

if(UNIX)
    find_library(PROTOBUF_LIBRARY protobuf)
elseif(WIN32)
    find_package(protobuf CONFIG REQUIRED)
    include_directories(${Protobuf_INCLUDE_DIRS})
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()

if(WIN32)
    ADD_DEFINITIONS(-DNOMINMAX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
elseif(UNIX)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -Wall -D_DEBUG")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -Wall -DNDEBUG")
    if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    endif()
endif()

file(GLOB GAYRPC_CORE_PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/src/gayrpc/core/*.h)
file(GLOB GAYRPC_PROTOCOL_PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/src/gayrpc/protocol/*.h)
file(GLOB GAYRPC_UTILS_PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/src/gayrpc/utils/*.h)

set(gayrpc_dependent_src_list
    ${PROJECT_SOURCE_DIR}/src/gayrpc/core/gayrpc_option.pb.cc
    ${PROJECT_SOURCE_DIR}/src/gayrpc/core/gayrpc_meta.pb.cc
)

add_library(gayrpc ${gayrpc_dependent_src_list})

add_subdirectory(examples/echo)
add_subdirectory(examples/benchmark)

if(WIN32)
  if (MSVC_VERSION VERSION_GREATER 1910)
    add_subdirectory(tests)
  endif()
elseif(UNIX)
  if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0) 
    add_subdirectory(tests)
  endif()
endif()