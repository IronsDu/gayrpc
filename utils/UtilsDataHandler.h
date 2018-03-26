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
                return false;
            }

            if (handleRpcEventLoop != nullptr)
            {
                handleRpcEventLoop->pushAsyncProc([rpcHandlerManager, meta, msg]() {
                    rpcHandlerManager->handleRpcMsg(meta, msg.data);
                });
                return true;
            }
            else
            {
                return rpcHandlerManager->handleRpcMsg(meta, msg.data);
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
    const std::weak_ptr<brynet::net::TCPSession>& weakSession)
{
    // 实际的发送
    BigPacket bpw(true, true);
    gayrpc::oppacket::serializeProtobufPacket(bpw,
        meta.SerializeAsString(),
        message.SerializeAsString());

    auto session = weakSession.lock();
    if (session != nullptr)
    {
        session->send(bpw.getData(), bpw.getPos());
    }
    else
    {
        // throw ConnectionClose exception
    }
    next(meta, message);
}

#endif