FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(echoclient
EchoClient.cpp
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(echoclient protobuf::libprotobuf gayrpc ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoclient PRIVATE gayrpc pthread protobuf)
endif()

add_executable(sync_echoclient
SyncEchoClient.cpp
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(sync_echoclient protobuf::libprotobuf gayrpc ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(sync_echoclient PRIVATE gayrpc pthread protobuf)
endif()

add_executable(echoserver
EchoServer.cpp 
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(echoserver protobuf::libprotobuf gayrpc ws2_32)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoserver PRIVATE gayrpc pthread protobuf)
endif()
