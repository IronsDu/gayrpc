enable_testing()

add_executable(test_interceptor TestInterceptor.cpp)
if(WIN32)
  target_link_libraries(test_interceptor protobuf::libprotobuf gayrpc ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(test_interceptor PRIVATE gayrpc pthread protobuf)
endif()

add_test(TestInterceptor test_interceptor)

add_executable(test_rpc ./pb/echo_service.pb.cc TestRPC.cpp)
if(WIN32)
  target_link_libraries(test_rpc protobuf::libprotobuf gayrpc ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(test_rpc PRIVATE gayrpc pthread protobuf)
endif()

add_test(TestRPC test_rpc)