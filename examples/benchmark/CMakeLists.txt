FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(benchmark_client
BenchmarkClient.cpp
./pb/benchmark_service.pb.cc)
if(WIN32)
  target_link_libraries(benchmark_client ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_client pthread)
endif()
target_link_libraries(benchmark_client PRIVATE gayrpc protobuf::libprotobuf Folly::folly Folly::folly_deps)

add_executable(benchmark_server
BenchmarkServer.cpp
./pb/benchmark_service.pb.cc)

if(WIN32)
  target_link_libraries(benchmark_server ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_server pthread)
endif()
target_link_libraries(benchmark_server PRIVATE gayrpc protobuf::libprotobuf Folly::folly Folly::folly_deps)