FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(benchmark_client
BenchmarkClient.cpp
./pb/benchmark_service.pb.cc)

if(WIN32)
  target_link_libraries(benchmark_client protobuf::libprotobuf gayrpc ws2_32)
  target_link_libraries(benchmark_client PRIVATE Folly::folly Folly::folly_deps Folly::follybenchmark Folly::folly_test_util)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_client gayrpc pthread protobuf)
endif()

add_executable(benchmark_server
BenchmarkServer.cpp
./pb/benchmark_service.pb.cc)

if(WIN32)
  target_link_libraries(benchmark_server protobuf::libprotobuf gayrpc ws2_32)
  target_link_libraries(benchmark_server PRIVATE Folly::folly Folly::folly_deps Folly::follybenchmark Folly::folly_test_util)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_server gayrpc pthread protobuf)
endif()