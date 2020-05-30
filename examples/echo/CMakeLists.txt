FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(echoclient
EchoClient.cpp
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(echoclient gayrpc ws2_32)
  if(DEFINED ENV{APPVEYOR_CI})
    target_link_libraries(echoclient protobuf::libprotobuf)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoclient PRIVATE gayrpc pthread protobuf)
endif()

add_executable(sync_echoclient
SyncEchoClient.cpp
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(sync_echoclient gayrpc ws2_32)
  if(DEFINED ENV{APPVEYOR_CI})
    target_link_libraries(sync_echoclient protobuf::libprotobuf)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(sync_echoclient PRIVATE gayrpc pthread protobuf)
endif()

add_executable(echoserver
EchoServer.cpp 
./pb/echo_service.pb.cc)

if(WIN32)
  target_link_libraries(echoserver gayrpc ws2_32)
  if(DEFINED ENV{APPVEYOR_CI})
    target_link_libraries(echoserver protobuf::libprotobuf)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoserver PRIVATE gayrpc pthread protobuf)
endif()
