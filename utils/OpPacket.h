#ifndef _OP_PACKET_H
#define _OP_PACKET_H

#include <string>

#include <brynet/utils/packet.h>

// 实现协议解析和序列化

namespace gayrpc
{
    namespace oppacket
    {
        typedef uint32_t OpCodeType;
        enum OpCode : OpCodeType
        {
            OpCodeProtobuf = 1,
        };

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
            }head;

            std::string meta;
            std::string data;
        };

        typedef std::function<void(const ProtobufPacket&)> ProtobufPacketHandler;

        typedef std::function<bool(const OpPacket&)>
            OpPacketHandler;

        // 解析网络消息中的OpPacket
        static size_t parseOpPacket(const char* buffer,
            size_t len,
            const OpPacketHandler& handler)
        {
            size_t processLen = 0;

            while (len > processLen)
            {
                BasePacketReader bpr(buffer + processLen, len - processLen);
                OpPacket opPacket;

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

            protobufPacket.meta = std::string(bpr.getBuffer() + bpr.getPos(),
                protobufPacket.head.meta_size);
            bpr.addPos(protobufPacket.head.meta_size);

            protobufPacket.data = std::string(bpr.getBuffer() + bpr.getPos(),
                protobufPacket.head.data_size);
            bpr.addPos(protobufPacket.head.data_size);

            handler(protobufPacket);

            return true;
        }

        static void serializeProtobufPacket(BasePacketWriter& bpw,
            const std::string& meta,
            const std::string& data)
        {
            ProtobufPacket protobufPacket;
            protobufPacket.head.meta_size = meta.size();
            protobufPacket.head.data_size = data.size();

            OpPacket opPacket;
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
    }
}

#endif