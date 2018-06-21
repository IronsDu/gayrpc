#ifndef _UTILS_RPC_DATA_HANDLER_H
#define _UTILS_RPC_DATA_HANDLER_H

#include <brynet/net/WrapTCPService.h>
#include "OpPacket.h"
#include "GayRpcCore.h"
#include "GayRpcTypeHandler.h"

static size_t dataHandle(const gayrpc::core::RpcTypeHandleManager::PTR& rpcHandlerManager,
    const char* buffer,
    size_t len,
    brynet::net::EventLoop::PTR handleRpcEventLoop = nullptr)
{
    auto opHandle = [rpcHandlerManager, handleRpcEventLoop](const gayrpc::oppacket::OpPacket& opPacket) {
        if (opPacket.head.op != gayrpc::oppacket::OpCode::OpCodeProtobuf)
        {
            return false;
        }

        auto pbPacketHandle = [rpcHandlerManager, handleRpcEventLoop](const gayrpc::oppacket::ProtobufPacket& msg) {
            gayrpc::core::RpcMeta meta;
            if (!meta.ParseFromString(msg.meta))
            {
                std::cerr << "parse RpcMeta protobuf failed" << std::endl;
                return;
            }

            if (handleRpcEventLoop != nullptr)
            {
                handleRpcEventLoop->pushAsyncProc([rpcHandlerManager, meta, msg]() {
                    try
                    {
                        rpcHandlerManager->handleRpcMsg(meta, msg.data);
                    }
                    catch (const std::runtime_error& e)
                    {
                        std::cerr << e.what() << std::endl;
                    }
                    catch(...)
                    { }
                });
            }
            else
            {
                try
                {
                    rpcHandlerManager->handleRpcMsg(meta, msg.data);
                }
                catch(const std::runtime_error& e)
                {
                    std::cerr << e.what() << std::endl;
                }
                catch (...)
                {

                }
            }
        };

        if (!parseProtobufPacket(opPacket, pbPacketHandle))
        {
            std::cout << "parse protobuf packet failed" << std::endl;
            return false;
        }

        return true;
    };

    return gayrpc::oppacket::parseOpPacket(buffer, len, opHandle);
}

static void sender(const gayrpc::core::RpcMeta& meta,
    const google::protobuf::Message& message,
    const gayrpc::core::UnaryHandler& next,
    const brynet::net::TCPSession::WEAK_PTR& weakSession)
{
    // 实际的发送
    AutoMallocPacket<4096> bpw(true, true);
    gayrpc::oppacket::serializeProtobufPacket(bpw,
        meta.SerializeAsString(),
        message.SerializeAsString());

    auto session = weakSession.lock();
    if (session != nullptr)
    {
        session->send(bpw.getData(), bpw.getPos());
    }
    next(meta, message);
}

#endif