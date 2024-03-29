#pragma once

#include <gayrpc/core/GayRpcTypeHandler.h>

#include <brynet/base/Packet.hpp>
#include <brynet/net/EventLoop.hpp>
#include <brynet/net/SendableMsg.hpp>
#include <brynet/net/TcpConnection.hpp>
#include <string>
#include <string_view>

// 实现协议解析和序列化

namespace gayrpc::protocol {

using namespace gayrpc::core;
using namespace brynet::base;
using namespace brynet::net;

class binary
{
public:
    using OpCodeType = uint32_t;
    enum class OpCode : OpCodeType
    {
        OpCodeProtobuf = 1,
    };

    static void send(const gayrpc::core::RpcMeta& meta,
                     const google::protobuf::Message& message,
                     const std::weak_ptr<brynet::net::TcpConnection>& weakSession)
    {
        class BinaryRpcSendMsg : public SendableMsg
        {
        public:
            BinaryRpcSendMsg(const gayrpc::core::RpcMeta& meta,
                             const google::protobuf::Message& message)
                : mPacket(false, true)
            {
                serializeProtobufPacket(mPacket,
                                        meta.SerializeAsString(),
                                        message.SerializeAsString());
            }

            virtual const void* data() override
            {
                return mPacket.getData();
            }

            virtual size_t size() override
            {
                return mPacket.getPos();
            }

        private:
            AutoMallocPacket<4096> mPacket;
        };

        const auto session = weakSession.lock();
        if (session == nullptr)
        {
            return;
        }

        auto msg = std::make_shared<BinaryRpcSendMsg>(meta, message);
        session->send(msg);
    }

    static void binaryPacketHandle(const gayrpc::core::RpcTypeHandleManager::Ptr& rpcHandlerManager,
                                   brynet::base::BasePacketReader& reader)
    {
        auto opHandle = [rpcHandlerManager](const OpPacket& opPacket) {
            if (opPacket.head.op != static_cast<OpCodeType>(OpCode::OpCodeProtobuf))
            {
                // only support protobuf binary protocol
                return false;
            }

            auto pbPacketHandle = [rpcHandlerManager](const ProtobufPacket& msg) {
                gayrpc::core::RpcMeta meta;
                if (!meta.ParseFromArray(msg.meta_view.data(), static_cast<int>(msg.meta_view.size())))
                {
                    std::cerr << "parse RpcMeta protobuf failed" << std::endl;
                    return;
                }

                try
                {
                    rpcHandlerManager->handleRpcMsg(std::move(meta),
                                                    msg.data_view,
                                                    InterceptorContextType{});
                }
                catch (const std::exception& e)
                {
                    std::cerr << "handle rpc cause exception:" << e.what() << std::endl;
                }
                catch (...)
                {
                    std::cerr << "handle rpc cause unknown exception" << std::endl;
                }
            };

            if (!parseProtobufPacket(opPacket, pbPacketHandle))
            {
                std::cout << "parse protobuf packet failed" << std::endl;
                return false;
            }

            return true;
        };

        parseOpPacket(reader, opHandle);
    }

private:
    // 基于[len, op] 的消息包格式
    struct OpPacket
    {
        // header部分
        struct
        {
            // data部分的长度
            uint64_t data_len;
            // opcode
            OpCodeType op;
        } head;

        // data部分
        const char* data;
    };

    // protobuf RPC 消息包格式
    struct ProtobufPacket
    {
        // header部分
        struct
        {
            uint32_t meta_size;// 4 bytes
            uint64_t data_size;// 8 bytes
        } head{};

        std::string_view meta_view;
        std::string_view data_view;
    };

    static void serializeProtobufPacket(BasePacketWriter& bpw,
                                        const std::string& meta,
                                        const std::string& data)
    {
        const auto bodyLen = sizeof(ProtobufPacket::head.meta_size) + sizeof(ProtobufPacket::head.data_size) + meta.size() + data.size();

        bpw.writeUINT64(bodyLen);
        bpw.writeUINT32(static_cast<OpCodeType>(OpCode::OpCodeProtobuf));

        bpw.writeUINT32(meta.size());
        bpw.writeUINT64(data.size());
        bpw.writeBinary(meta);
        bpw.writeBinary(data);
    }

    using ProtobufPacketHandler = std::function<void(const ProtobufPacket&)>;
    using OpPacketHandler = std::function<bool(const OpPacket&)>;

    // 解析网络消息中的OpPacket
    template<typename OpPacketHandler>
    static void parseOpPacket(brynet::base::BasePacketReader& reader,
                              const OpPacketHandler& handler)
    {
        OpPacket opPacket{};
        while (reader.enough(sizeof(opPacket.head.data_len) + sizeof(opPacket.head.op)))
        {
            opPacket.head.data_len = reader.readUINT64();
            opPacket.head.op = reader.readUINT32();

            if (!reader.enough(opPacket.head.data_len))
            {
                break;
            }

            opPacket.data = reader.currentBuffer();
            handler(opPacket);

            reader.addPos(opPacket.head.data_len);
            reader.savePos();
        }
    }

    // 解析OpPacket中的protobuf packet
    template<typename ProtobufPacketHandler>
    static bool parseProtobufPacket(const OpPacket& opPacket,
                                    const ProtobufPacketHandler& handler)
    {
        BasePacketReader bpr(opPacket.data, opPacket.head.data_len);
        ProtobufPacket protobufPacket;

        if (!bpr.enough(sizeof(protobufPacket.head.meta_size) +
                        sizeof(protobufPacket.head.data_size)))
        {
            return false;
        }

        protobufPacket.head.meta_size = bpr.readUINT32();
        protobufPacket.head.data_size = bpr.readUINT64();

        if (!bpr.enough(protobufPacket.head.meta_size +
                        protobufPacket.head.data_size))
        {
            return false;
        }

        protobufPacket.meta_view = std::string_view(bpr.currentBuffer(), protobufPacket.head.meta_size);
        bpr.addPos(protobufPacket.head.meta_size);

        protobufPacket.data_view = std::string_view(bpr.currentBuffer(), protobufPacket.head.data_size);
        bpr.addPos(protobufPacket.head.data_size);

        handler(protobufPacket);

        return true;
    }
};

}// namespace gayrpc::protocol
