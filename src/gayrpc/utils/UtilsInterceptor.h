#pragma once

#include <functional>
#include <memory>
#include <exception>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcInterceptor.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/protocol/HttpProtocol.h>

namespace gayrpc { namespace utils {

    using namespace gayrpc::core;

    // 一些辅助型拦截器

    static auto withEventLoop(brynet::net::EventLoop::Ptr eventLoop)
    {
        return [=](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context) {

                std::shared_ptr<google::protobuf::Message> msg;
                msg.reset(message.New());
                msg->CopyFrom(message);

                eventLoop->runAsyncFunctor([=]() {
                        next(meta, *msg, std::move(context));
                    });
        };
    }

    static auto withProtectedCall()
    {
        return [](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context) {
            try
            {
                next(meta, message, std::move(context));
            }
            catch (const std::exception& e)
            {
                std::cout << e.what() << std::endl;
            }
            catch (...)
            {
                std::cout << "unknow exception" << std::endl;
            }
        };
    }

    static auto withSessionBinarySender(std::weak_ptr<brynet::net::TcpConnection> weakSession)
    {
        return [weakSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context) {
            gayrpc::protocol::binary::send(meta, message, weakSession);
            next(meta, message, std::move(context));
        };
    }

    static void causeTimeout(const gayrpc::core::RpcTypeHandleManager::PTR& handleManager,
        uint64_t seq_id)
    {
        gayrpc::core::RpcMeta timeoutMeta;
        timeoutMeta.set_type(gayrpc::core::RpcMeta::RESPONSE);
        timeoutMeta.mutable_response_info()->set_timeout(true);
        timeoutMeta.mutable_response_info()->set_sequence_id(seq_id);
        try
        {
            InterceptorContextType context;
            handleManager->handleRpcMsg(timeoutMeta, "", std::move(context));
        }
        catch (...)
        {
        }
    }

    // 由eventLoop线程处理超时检测
    static auto withTimeoutCheck(const brynet::net::EventLoop::Ptr& eventLoop,
        const gayrpc::core::RpcTypeHandleManager::PTR& handleManager)
    {
        return [eventLoop, handleManager](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context) {

            if (meta.request_info().timeout() > 0)
            {
                auto seqID = meta.request_info().sequence_id();
                auto timeoutSecond = meta.request_info().timeout();

                eventLoop->runAfter(std::chrono::seconds(timeoutSecond),
                    [seqID, handleManager]() {
                        causeTimeout(handleManager, seqID);
                    });
            }

            next(meta, message, std::move(context));
        };
    }

    static auto withHttpSessionSender(const brynet::net::http::HttpSession::Ptr& httpSession)
    {
        return [httpSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context) {
            gayrpc::protocol::http::send(meta, message, httpSession);
            httpSession->postShutdown();
            next(meta, message, std::move(context));
        };
    }

} }