#pragma once

#include <string>
#include <string_view>

#include <gayrpc/core/GayRpcTypeHandler.h>
#include <brynet/base/Packet.hpp>
#include <brynet/net/TcpConnection.hpp>
#include <brynet/net/EventLoop.hpp>

// 实现协议解析和序列化

namespace gayrpc::protocol {

    using namespace gayrpc::core;
    using namespace brynet::base;

    class binary
    {
    public:
        typedef uint32_t OpCodeType;
        enum OpCode : OpCodeType
        {
            OpCodeProtobuf = 1,
        };

        static void send(const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const std::weak_ptr<brynet::net::TcpConnection>& weakSession)
        {
            auto session = weakSession.lock();
            if (session == nullptr)
            {
                return;
            }

            if (session->getEventLoop()->isInLoopThread())
            {
                // 实际的发送
                AutoMallocPacket<4096> bpw(true, true);
                serializeProtobufPacket(bpw,
                    meta.SerializeAsString(),
                    message.SerializeAsString());

                session->send(bpw.getData(), bpw.getPos());
            }
            else
            {
                std::shared_ptr<google::protobuf::Message> msg;
                msg.reset(message.New());
                msg->CopyFrom(message);
                session->getEventLoop()->runAsyncFunctor([meta, msg, session]()
                {
                    // 实际的发送
                    AutoMallocPacket<4096> bpw(true, true);
                    serializeProtobufPacket(bpw,
                        meta.SerializeAsString(),
                        msg->SerializeAsString());

                    session->send(bpw.getData(), bpw.getPos());
                });
            }
        }

        static size_t binaryPacketHandle(const gayrpc::core::RpcTypeHandleManager::Ptr& rpcHandlerManager,
            const char* buffer,
            size_t len)
        {
            auto opHandle = [rpcHandlerManager](const OpPacket& opPacket) {
                if (opPacket.head.op != OpCode::OpCodeProtobuf)
                {
                    return false;
                }

                auto pbPacketHandle = [rpcHandlerManager](const ProtobufPacket& msg) {
                    gayrpc::core::RpcMeta meta;
                    if (!meta.ParseFromArray(msg.meta_view.data(), static_cast<int>(msg.meta_view.size())))
                    {
                        std::cerr << "parse RpcMeta protobuf failed" << std::endl;
                        return;
                    }
                    InterceptorContextType context;
                    try
                    {
                        rpcHandlerManager->handleRpcMsg(std::move(meta),
                                                        msg.data_view,
                                                        std::move(context));
                    }
                    catch (const std::runtime_error& e)
                    {
                        std::cerr << "handle rpc cause exception:" << e.what()<< std::endl;
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

            return parseOpPacket(buffer, len, opHandle);
        }

        static void serializeProtobufPacket(BasePacketWriter& bpw,
            const std::string& meta,
            const std::string& data)
        {
            SerializeProtobufPacket protobufPacket{};
            protobufPacket.head.meta_size = meta.size();
            protobufPacket.head.data_size = data.size();

            OpPacket opPacket{};
            opPacket.head.op = OpCodeProtobuf;
            opPacket.head.data_len = sizeof(protobufPacket.head.meta_size) +
                sizeof(protobufPacket.head.data_size) +
                protobufPacket.head.meta_size +
                protobufPacket.head.data_size;

            bpw.writeUINT64(opPacket.head.data_len);
            bpw.writeUINT32(opPacket.head.op);

            bpw.writeUINT32(protobufPacket.head.meta_size);
            bpw.writeUINT64(protobufPacket.head.data_size);

            bpw.writeBinary(meta);
            bpw.writeBinary(data);
        }

    private:

        // 基于[len, op] 的消息包格式
        struct OpPacket
        {
            // header部分
            struct
            {
                // data部分的长度
                uint64_t     data_len;
                // opcode
                OpCodeType     op;
            }head;

            // data部分
            const char* data;
        };

        // protobuf RPC 消息包格式
        struct ProtobufPacket
        {
            // header部分
            struct
            {
                uint32_t   meta_size;    // 4 bytes
                uint64_t   data_size;    // 8 bytes
            }head{};

            std::string_view    meta_view;
            std::string_view    data_view;
        };

        struct SerializeProtobufPacket
        {
            // header部分
            struct
            {
                uint32_t   meta_size;    // 4 bytes
                uint64_t   data_size;    // 8 bytes
            }head;
        };

        using ProtobufPacketHandler = std::function<void(const ProtobufPacket&)>;
        using OpPacketHandler = std::function<bool(const OpPacket&)>;

        // 解析网络消息中的OpPacket
        template<typename OpPacketHandler>
        static size_t parseOpPacket(const char* buffer,
            size_t len,
            const OpPacketHandler& handler)
        {
            size_t processLen = 0;

            while (len > processLen)
            {
                BasePacketReader bpr(buffer + processLen, len - processLen);
                OpPacket opPacket{};

                constexpr auto HEAD_LEN =
                    sizeof(opPacket.head.data_len) +
                    sizeof(opPacket.head.op);

                if (bpr.getLeft() < HEAD_LEN)
                {
                    break;
                }

                opPacket.head.data_len = bpr.readUINT64();
                opPacket.head.op = bpr.readUINT32();

                if (bpr.getLeft() < opPacket.head.data_len)
                {
                    break;
                }

                opPacket.data = bpr.getBuffer() + bpr.getPos();
                handler(opPacket);
                bpr.addPos(opPacket.head.data_len);

                processLen += (HEAD_LEN + opPacket.head.data_len);
            }

            return processLen;
        }

        // 解析OpPacket中的protobuf packet
        template<typename ProtobufPacketHandler>
        static bool parseProtobufPacket(const OpPacket& opPacket,
            const ProtobufPacketHandler& handler)
        {
            BasePacketReader bpr(opPacket.data, opPacket.head.data_len);

            ProtobufPacket protobufPacket;

            constexpr auto HEAD_LEN =
                sizeof(protobufPacket.head.meta_size) +
                sizeof(protobufPacket.head.data_size);

            if (bpr.getLeft() < HEAD_LEN)
            {
                return false;
            }

            protobufPacket.head.meta_size = bpr.readUINT32();
            protobufPacket.head.data_size = bpr.readUINT64();

            if (bpr.getLeft() !=
                (protobufPacket.head.meta_size +
                    protobufPacket.head.data_size))
            {
                return false;
            }

            protobufPacket.meta_view = std::string_view(bpr.getBuffer() + bpr.getPos(),
                                                   protobufPacket.head.meta_size);

            bpr.addPos(protobufPacket.head.meta_size);

            protobufPacket.data_view = std::string_view(bpr.getBuffer() + bpr.getPos(),
                                                        protobufPacket.head.data_size);
            bpr.addPos(protobufPacket.head.data_size);

            handler(protobufPacket);

            return true;
        }
    };
    
}
