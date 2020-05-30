FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(benchmark_client
BenchmarkClient.cpp
./pb/benchmark_service.pb.cc)

if(WIN32)
  target_link_libraries(benchmark_client gayrpc ws2_32)
  if(DEFINED ENV{APPVEYOR_CI})
    target_link_libraries(benchmark_client protobuf::libprotobuf)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_client gayrpc pthread protobuf)
endif()

add_executable(benchmark_server
BenchmarkServer.cpp
./pb/benchmark_service.pb.cc)

if(WIN32)
  target_link_libraries(benchmark_server gayrpc ws2_32)
  if(DEFINED ENV{APPVEYOR_CI})
    target_link_libraries(benchmark_server protobuf::libprotobuf)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(benchmark_server gayrpc pthread protobuf)
endif()
