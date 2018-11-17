FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(benchmark_client
BenchmarkClient.cpp
./pb/benchmark_service.pb.cc
../../src/gayrpc/core/gayrpc_option.pb.cc
../../src/gayrpc/core/meta.pb.cc
${HEADER_LIST})

if(WIN32)
  target_link_libraries(benchmark_client ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_client pthread brynet crypto ssl protobuf)
endif()

add_executable(benchmark_server
BenchmarkServer.cpp
./pb/benchmark_service.pb.cc
../../src/gayrpc/core/gayrpc_option.pb.cc
../../src/gayrpc/core/meta.pb.cc
${HEADER_LIST})

if(WIN32)
  target_link_libraries(benchmark_server ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_server pthread brynet crypto ssl protobuf)
endif()
