#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>

namespace gayrpc { namespace core {

    class BaseReply
    {
    public:
        BaseReply(RpcMeta&& meta, UnaryServerInterceptor&& outboundInterceptor)
            :
            mRequestMeta(std::forward<RpcMeta>(meta)),
            mOutboundInterceptor(std::forward<UnaryServerInterceptor>(outboundInterceptor))
        {
        }

        virtual ~BaseReply() = default;

    protected:
        void    reply(const google::protobuf::Message& response, InterceptorContextType&& context)
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

            mOutboundInterceptor(std::move(meta), 
                response, 
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                }, 
                std::forward<InterceptorContextType>(context));
        }

        template<typename Response>
        void    error(int32_t errorCode, const std::string& reason, InterceptorContextType&& context)
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
            meta.set_encoding(RpcMeta_DataEncodingType_BINARY);
            meta.set_service_id(mRequestMeta.service_id());
            meta.mutable_response_info()->set_sequence_id(mRequestMeta.request_info().sequence_id());
            meta.mutable_response_info()->set_failed(true);
            meta.mutable_response_info()->set_error_code(errorCode);
            meta.mutable_response_info()->set_reason(reason);
            meta.mutable_response_info()->set_timeout(false);

            Response response;
            mOutboundInterceptor(meta, 
                response, 
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                }, 
                std::forward<InterceptorContextType>(context));
        }

    private:
        const RpcMeta                   mRequestMeta;
        const UnaryServerInterceptor    mOutboundInterceptor;
        std::atomic_flag                mReplyFlag = ATOMIC_FLAG_INIT;
    };

    template<typename T>
    class TemplateReply : public BaseReply
    {
    public:
        typedef std::shared_ptr<TemplateReply<T>> PTR;

        TemplateReply(RpcMeta&& meta,
            UnaryServerInterceptor&& outboundInterceptor)
            :
            BaseReply(std::move(meta), std::move(outboundInterceptor))
        {
        }

        void    reply(const T& response, InterceptorContextType&& context = InterceptorContextType())
        {
            BaseReply::reply(response, std::forward<InterceptorContextType>(context));
        }

        void    error(int32_t errorCode, const std::string& reason, InterceptorContextType&& context = InterceptorContextType())
        {
            BaseReply::error<T>(errorCode, reason, std::forward<InterceptorContextType>(context));
        }
    };

} }
