#pragma once

#include <gayrpc/core/GayRpcError.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <google/protobuf/util/json_util.h>

#include <functional>
#include <memory>
#include <string>

namespace gayrpc::core {

template<typename T>
inline auto MakeReadyFuture(T value)
{
    folly::Promise<T> promise;
    auto future = promise.getFuture();
    promise.setValue(std::move(value));
    return future;
}

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

template<typename Response, typename Hanele>
inline auto parseResponseWrapper(Hanele handle,
                                 RpcMeta&& meta,
                                 const std::string_view& data,
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
                throw std::runtime_error(std::string("parse binary response failed, type of:") + typeid(Response).name());
            }
            break;
        case RpcMeta::JSON:
            if (auto s = JsonStringToMessage(google::protobuf::StringPiece(data.data(), data.size()), &response); !s.ok())
            {
                throw std::runtime_error(std::string("parse json response failed:") + s.error_message().as_string() + ", type of:" + typeid(Response).name());
            }
            break;
        default:
            throw std::runtime_error(std::string("response by unsupported encoding:") + std::to_string(meta.encoding()) + ", type of:" + typeid(Response).name());
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
            [handle = std::move(handle), error = std::move(error)](RpcMeta&&, const google::protobuf::Message& msg, InterceptorContextType&& context) {
                handle(static_cast<const Response&>(msg), error);
                return MakeReadyFuture(std::optional<std::string>(std::nullopt));
            },
            std::move(context));
}

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
                throw std::runtime_error(std::string("parse binary request failed, type of:") + typeid(RequestType).name());
            }
            break;
        case RpcMeta::JSON:
            if (auto s = JsonStringToMessage(google::protobuf::StringPiece(data.data(), data.size()), &request); !s.ok())
            {
                throw std::runtime_error(std::string("parse json request failed:") + s.error_message().as_string() + ", type of:" + typeid(RequestType).name());
            }
            break;
        default:
            throw std::runtime_error(std::string("request by unsupported encoding:") + std::to_string(meta.encoding()) + ", type of:" + typeid(RequestType).name());
    }

    return inboundInterceptor(std::move(meta), request, std::move(handler), std::move(context));
}

}// namespace gayrpc::core
