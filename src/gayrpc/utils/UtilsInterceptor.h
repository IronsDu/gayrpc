#pragma once

#include <functional>
#include <memory>
#include <exception>
#include <iostream>

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
        return [=](gayrpc::core::RpcMeta&& meta,
            const google::protobuf::Message& message,
            gayrpc::core::UnaryHandler&& next,
            InterceptorContextType&& context) mutable {
                if(eventLoop->isInLoopThread())
                {
                    return next(std::forward<gayrpc::core::RpcMeta>(meta),
                         message,
                         std::forward<InterceptorContextType>(context));
                }
                else
                {
                    ananas::Promise<std::optional<std::string>> promise;

                    std::shared_ptr<google::protobuf::Message> msg(message.New());
                    msg->CopyFrom(message);

                    eventLoop->runAsyncFunctor([=,
                        meta = std::forward<gayrpc::core::RpcMeta>(meta),
                        context = std::forward<InterceptorContextType>(context),
                        next = std::forward<gayrpc::core::UnaryHandler>(next)]() mutable {
                            next(std::forward<gayrpc::core::RpcMeta>(meta),
                                *msg,
                                std::forward<InterceptorContextType>(context))
                                .Then([=](std::optional<std::string> err) mutable {
                                    promise.SetValue(err);
                                });
                        });
                    return promise.GetFuture();
                }
        };
    }

    static auto withProtectedCall()
    {
        return [](gayrpc::core::RpcMeta&& meta,
            const google::protobuf::Message& message,
            gayrpc::core::UnaryHandler&& next,
            InterceptorContextType&& context) {
            try
            {
                return next(std::forward<gayrpc::core::RpcMeta>(meta),
                     message,
                     std::forward<InterceptorContextType>(context));
            }
            catch (const std::exception& e)
            {
                std::cout << e.what() << std::endl;
                return ananas::MakeReadyFuture(std::optional<std::string>(e.what()));
            }
            catch (...)
            {
                std::cout << "unknow exception" << std::endl;
                return ananas::MakeReadyFuture(std::optional<std::string>("unknow exception"));
            }

            return ananas::MakeReadyFuture(std::optional<std::string>(std::nullopt));
        };
    }

    static auto withSessionBinarySender(std::weak_ptr<brynet::net::TcpConnection> weakSession)
    {
        return [weakSession](gayrpc::core::RpcMeta&& meta,
            const google::protobuf::Message& message,
            gayrpc::core::UnaryHandler&& next,
            InterceptorContextType&& context) {
            gayrpc::protocol::binary::send(meta, message, weakSession);
            return next(std::forward<gayrpc::core::RpcMeta>(meta),
                 message,
                 std::forward<InterceptorContextType>(context));
        };
    }

    static void causeTimeout(const gayrpc::core::RpcTypeHandleManager::Ptr& handleManager,
        uint64_t seq_id)
    {
        gayrpc::core::RpcMeta timeoutMeta;
        timeoutMeta.set_type(gayrpc::core::RpcMeta::RESPONSE);
        timeoutMeta.mutable_response_info()->set_timeout(true);
        timeoutMeta.mutable_response_info()->set_sequence_id(seq_id);

        InterceptorContextType context;
        try
        {
            handleManager->handleRpcMsg(std::forward<gayrpc::core::RpcMeta>(timeoutMeta),
                                        "",
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
    }

    // 由eventLoop线程处理超时检测
    static auto withTimeoutCheck(const brynet::net::EventLoop::Ptr& eventLoop,
        const gayrpc::core::RpcTypeHandleManager::Ptr& handleManager)
    {
        return [eventLoop, handleManager](gayrpc::core::RpcMeta&& meta,
            const google::protobuf::Message& message,
            gayrpc::core::UnaryHandler&& next,
            InterceptorContextType&& context) {

            if (meta.has_request_info() && meta.request_info().timeout() > 0)
            {
                auto seqID = meta.request_info().sequence_id();
                auto timeoutSecond = meta.request_info().timeout();

                eventLoop->runAfter(std::chrono::seconds(timeoutSecond),
                    [seqID, handleManager]() {
                        causeTimeout(handleManager, seqID);
                    });
            }

            return next(std::forward<gayrpc::core::RpcMeta>(meta),
                 message,
                 std::forward<InterceptorContextType>(context));
        };
    }

    static auto withHttpSessionSender(const brynet::net::http::HttpSession::Ptr& httpSession)
    {
        return [httpSession](gayrpc::core::RpcMeta&& meta,
            const google::protobuf::Message& message,
            gayrpc::core::UnaryHandler&& next,
            InterceptorContextType&& context) {
            gayrpc::protocol::http::send(meta, message, httpSession);
            httpSession->postShutdown();
            return next(std::forward<gayrpc::core::RpcMeta>(meta),
                 message,
                 std::forward<InterceptorContextType>(context));
        };
    }

} }
