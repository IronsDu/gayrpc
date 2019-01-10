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
  target_link_libraries(test_rpc PRIVATE gayrpc pthread brynet ssl crypto protobuf)
endif()

add_test(TestRPC test_rpc)

add_executable(test_orleans 
./pb/echo_service.pb.cc 
./pb/orleans_service.pb.cc 
TestOrleans.cpp)

if(WIN32)
  target_link_libraries(test_orleans gayrpc ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  find_library(BRYNET_LIBRARY brynet)
  target_link_libraries(test_orleans PRIVATE gayrpc pthread ${BRYNET_LIBRARY} ssl crypto protobuf)
endif()

add_test(TestOrleans test_orleans)