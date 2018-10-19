FILE(GLOB_RECURSE HEADER_LIST
 *.h 
 ../../src/gayrpc/core/*.h
 ../../src/gayrpc/protocol/*.h
 ../../src/gayrpc/utils/*.h)

add_executable(echoclient
EchoClient.cpp
./pb/echo_service.pb.cc
../../src/gayrpc/core/gayrpc_option.pb.cc
../../src/gayrpc/core/meta.pb.cc
${HEADER_LIST})

if(WIN32)
  target_link_libraries(echoclient ws2_32 brynet)
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoclient PRIVATE pthread ${BRYNET_LIBRARY} protobuf::libprotobuf-lite protobuf::libprotobuf)
endif()

add_executable(sync_echoclient
SyncEchoClient.cpp
./pb/echo_service.pb.cc
../../src/gayrpc/core/gayrpc_option.pb.cc
../../src/gayrpc/core/meta.pb.cc
${HEADER_LIST})

if(WIN32)
  target_link_libraries(sync_echoclient ws2_32 ${BRYNET_LIBRARY})
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(sync_echoclient PRIVATE pthread ${BRYNET_LIBRARY} protobuf::libprotobuf-lite protobuf::libprotobuf)
endif()

add_executable(echoserver
EchoServer.cpp 
./pb/echo_service.pb.cc
../../src/gayrpc/core/gayrpc_option.pb.cc
../../src/gayrpc/core/meta.pb.cc
${HEADER_LIST})

if(WIN32)
  target_link_libraries(echoserver ws2_32 ${BRYNET_LIBRARY})
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(echoserver PRIVATE pthread ${BRYNET_LIBRARY} protobuf::libprotobuf-lite protobuf::libprotobuf)
endif()
