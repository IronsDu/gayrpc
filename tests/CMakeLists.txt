enable_testing()

add_executable(test_interceptor TestInterceptor.cpp)
if(WIN32)
  target_link_libraries(test_interceptor gayrpc ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(test_interceptor PRIVATE gayrpc pthread brynet ssl crypto protobuf)
endif()

add_test(TestInterceptor test_interceptor)

add_executable(test_rpc ./pb/echo_service.pb.cc TestRPC.cpp)
if(WIN32)
  target_link_libraries(test_rpc gayrpc ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(test_rpc PRIVATE PRIVATE gayrpc pthread brynet ssl crypto protobuf)
endif()

add_test(TestRPC test_rpc)