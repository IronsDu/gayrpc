#pragma once

#include <functional>
#include <memory>
#include <string>

#include <google/protobuf/util/json_util.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcError.h>

namespace gayrpc::core {

    // 构造用于RPC请求的Meta对象
    inline RpcMeta makeRequestRpcMeta(uint64_t sequenceID,
        ServiceIDType serviceID,
        ServiceFunctionMsgIDType msgID,
        RpcMeta_DataEncodingType type,
        bool expectResponse)
    {
        RpcMeta meta;
        meta.set_type(RpcMeta::REQUEST);
        meta.set_encoding(type);
        meta.set_service_id(serviceID);
        meta.mutable_request_info()->set_intmethod(msgID);
        meta.mutable_request_info()->set_sequence_id(sequenceID);
        meta.mutable_request_info()->set_expect_response(expectResponse);

        return meta;
    }

    // 解析Response然后(通过拦截器)调用回调
    template<typename Response, typename Hanele>
    inline auto    parseResponseWrapper(const Hanele& handle,
        RpcMeta&& meta,
        const std::string_view & data,
        const UnaryServerInterceptor& inboundInterceptor,
        InterceptorContextType&& context)
    {
        using namespace google::protobuf::util;

        Response response;
        switch (meta.encoding())
        {
        case RpcMeta::BINARY:
            if (!response.ParseFromArray(data.data(), static_cast<int>(data.size())))
            {
                throw std::runtime_error(std::string("parse binary response failed, type of:")
                                         + typeid(Response).name());
            }
            break;
        case RpcMeta::JSON:
            {
                if (auto s = JsonStringToMessage(google::protobuf::StringPiece(data.data(), data.size()), &response); !s.ok())
                {
                    throw std::runtime_error(std::string("parse json response failed:")
                                             + s.error_message().as_string()
                                             + ", type of:" + typeid(Response).name());
                }
            }
            break;
        default:
            throw std::runtime_error(std::string("response by unsupported encoding:")
                                     + std::to_string(meta.encoding())
                                     + ", type of:"
                                     + typeid(Response).name());
        }

        std::optional<gayrpc::core::RpcError> error;
        if (meta.response_info().failed())
        {
            error = gayrpc::core::RpcError(meta.response_info().error_code(),
                meta.response_info().reason());
        }
        return inboundInterceptor(
                std::move(meta),
                response,
                [=](RpcMeta&&, const google::protobuf::Message& msg, InterceptorContextType&& context)
                {
                    handle(response, error);
                    return ananas::MakeReadyFuture(std::optional<std::string>(std::nullopt));
                },
                std::move(context));
    }

    // 解析Request然后(通过拦截器)调用服务处理函数
    template<typename RequestType, typename UnaryServerInterceptor>
    inline auto parseRequestWrapper(RequestType& request,
        RpcMeta&& meta,
        const std::string_view& data,
        const UnaryServerInterceptor& inboundInterceptor,
        UnaryHandler&& handler,
        InterceptorContextType&& context)
    {
        using namespace google::protobuf::util;

        switch (meta.encoding())
        {
        case RpcMeta::BINARY:
            if (!request.ParseFromArray(data.data(), static_cast<int>(data.size())))
            {
                throw std::runtime_error(std::string("parse binary request failed, type of:")
                                         + typeid(RequestType).name());
            }
            break;
        case RpcMeta::JSON:
            {
                if (auto s = JsonStringToMessage(google::protobuf::StringPiece(data.data(), data.size()), &request); !s.ok())
                {
                    throw std::runtime_error(std::string("parse json request failed:")
                                             + s.error_message().as_string()
                                             + ", type of:"
                                             + typeid(RequestType).name());
                }
            }
            break;
        default:
            throw std::runtime_error(std::string("request by unsupported encoding:")
                                     + std::to_string(meta.encoding())
                                     + ", type of:"
                                     + typeid(RequestType).name());
        }

        return inboundInterceptor(std::move(meta), request, std::move(handler), std::move(context));
    }

}
