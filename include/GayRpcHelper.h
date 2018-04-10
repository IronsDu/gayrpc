#ifndef _GAY_RPC_HELPER_H
#define _GAY_RPC_HELPER_H

#include <functional>
#include <memory>

#include <google/protobuf/util/json_util.h>
#include "meta.pb.h"
#include "GayRpcCore.h"
#include "GayRpcError.h"

namespace gayrpc
{
    namespace core
    {
        using namespace google::protobuf::util;
        // 构造用于RPC请求的Meta对象
        inline RpcMeta makeRequestRpcMeta(uint64_t sequenceID,
            uint64_t msgID,
            RpcMeta_DataEncodingType type,
            bool expectResponse)
        {
            RpcMeta meta;
            meta.set_type(RpcMeta::REQUEST);
            meta.set_encoding(type);
            meta.mutable_request_info()->set_intmethod(msgID);
            meta.mutable_request_info()->set_sequence_id(sequenceID);
            meta.mutable_request_info()->set_expect_response(expectResponse);

            return meta;
        }

        // 解析Response然后(通过拦截器)调用回调
        template<typename Response, typename Hanele>
        inline void    parseWrapper(const Hanele& handle,
            const RpcMeta& meta,
            const std::string& data,
            const UnaryServerInterceptor& inboundInterceptor)
        {
            Response response;
            switch (meta.encoding())
            {
            case RpcMeta::BINARY:
                if (!response.ParseFromString(data))
                {
                    throw std::runtime_error("parse binary echo response failed");
                }
                break;
            case RpcMeta::JSON:
            {
                auto s = JsonStringToMessage(data, &response);
                if (!s.ok())
                {
                    throw std::runtime_error("parse json echo response failed:" +
                        s.error_message().as_string());
                }
                break;
            }
            default:
                throw std::runtime_error("unknow encoding" + meta.encoding());
            }

            gayrpc::core::RpcError error;
            if (meta.response_info().failed())
            {
                error = gayrpc::core::RpcError(meta.response_info().failed(),
                    meta.response_info().error_code(),
                    meta.response_info().reason());
            }

            inboundInterceptor(
                meta, 
                response, 
                [&response, &error, &handle](const RpcMeta&, const google::protobuf::Message&) {
                    handle(response, error);
                });
        }
    }
}

#endif
