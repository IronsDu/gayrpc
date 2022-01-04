#pragma once

#include <gayrpc/core/GayRpcHelper.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace gayrpc::core {

class BaseReply
{
public:
    BaseReply(RpcMeta&& meta, UnaryServerInterceptor&& outboundInterceptor)
        : mRequestMeta(std::move(meta)),
          mOutboundInterceptor(std::move(outboundInterceptor))
    {
    }

    virtual ~BaseReply() = default;

    void reply(const google::protobuf::Message& response, InterceptorContextType&& context)
    {
        if (mReplyFlag.test_and_set())
        {
            throw std::runtime_error("already reply");
        }

        if (!mRequestMeta.request_info().expect_response())
        {
            return;
        }

        RpcMeta meta;
        meta.set_type(RpcMeta::RESPONSE);
        meta.set_service_id(mRequestMeta.service_id());
        meta.mutable_response_info()->set_sequence_id(mRequestMeta.request_info().sequence_id());
        meta.mutable_response_info()->set_failed(false);
        meta.mutable_response_info()->set_timeout(false);

        mOutboundInterceptor(
                std::move(meta),
                response,
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                    return MakeReadyFuture(std::optional<std::string>(std::nullopt));
                },
                std::move(context));
    }

    template<typename Response>
    void error(int32_t errorCode, const std::string& reason, InterceptorContextType&& context)
    {
        if (mReplyFlag.test_and_set())
        {
            throw std::runtime_error("already reply");
        }

        if (!mRequestMeta.request_info().expect_response())
        {
            return;
        }

        ReplyError(mOutboundInterceptor, mRequestMeta.service_id(), mRequestMeta.request_info().sequence_id(), errorCode, reason, std::move(context));
    }

    static void ReplyError(const UnaryServerInterceptor& outboundInterceptor,
                           ServiceIDType serviceId,
                           uint64_t seqId,
                           int32_t errorCode,
                           const std::string& reason,
                           InterceptorContextType&& context)
    {
        RpcMeta meta;
        meta.set_type(RpcMeta::RESPONSE);
        meta.set_encoding(RpcMeta_DataEncodingType_BINARY);
        meta.set_service_id(serviceId);
        meta.mutable_response_info()->set_sequence_id(seqId);
        meta.mutable_response_info()->set_failed(true);
        meta.mutable_response_info()->set_error_code(errorCode);
        meta.mutable_response_info()->set_reason(reason);
        meta.mutable_response_info()->set_timeout(false);

        outboundInterceptor(
                std::move(meta),
                RpcMeta{},
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                    return MakeReadyFuture(std::optional<std::string>(std::nullopt));
                },
                std::move(context));
    }


private:
    const RpcMeta mRequestMeta;
    const UnaryServerInterceptor mOutboundInterceptor;
    std::atomic_flag mReplyFlag = ATOMIC_FLAG_INIT;
};

template<typename T>
class TemplateReply : public BaseReply
{
public:
    typedef std::shared_ptr<TemplateReply<T>> Ptr;

    TemplateReply(RpcMeta&& meta,
                  UnaryServerInterceptor&& outboundInterceptor)
        : BaseReply(std::move(meta), std::move(outboundInterceptor))
    {
    }

    void reply(const T& response, InterceptorContextType&& context = InterceptorContextType{})
    {
        BaseReply::reply(response, std::move(context));
    }

    void error(int32_t errorCode, const std::string& reason, InterceptorContextType&& context = InterceptorContextType{})
    {
        BaseReply::error<T>(errorCode, reason, std::move(context));
    }
};

}// namespace gayrpc::core
