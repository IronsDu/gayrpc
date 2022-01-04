#pragma once

#include <gayrpc/core/GayRpcHelper.h>
#include <gayrpc/core/GayRpcInterceptor.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/protocol/HttpProtocol.h>

#include <exception>
#include <functional>
#include <iostream>
#include <memory>

namespace gayrpc { namespace utils {

using namespace gayrpc::core;

// 一些辅助型拦截器

static auto withEventLoop(brynet::net::EventLoop::Ptr eventLoop)
{
    return [=](gayrpc::core::RpcMeta&& meta,
               const google::protobuf::Message& message,
               gayrpc::core::UnaryHandler&& next,
               InterceptorContextType&& context) mutable {
        if (eventLoop->isInLoopThread())
        {
            return next(std::move(meta),
                        message,
                        std::move(context));
        }
        else
        {
            std::shared_ptr<google::protobuf::Message> msg(message.New());
            msg->CopyFrom(message);

            auto promise = std::make_shared<folly::Promise<std::optional<std::string>>>();
            eventLoop->runAsyncFunctor([=,
                                        msg = std::move(msg),
                                        meta = std::move(meta),
                                        context = std::move(context),
                                        next = std::move(next)]() mutable {
                next(std::move(meta),
                     *msg,
                        std::move(context))
                        .thenValue([=](std::optional<std::string> err) mutable {
                            promise->setValue(err);
                        });
            });
            return promise->getFuture();
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
            return next(std::move(meta),
                        message,
                        std::move(context));
        }
        catch (const std::exception& e)
        {
            std::cout << e.what() << std::endl;
            return MakeReadyFuture(std::optional<std::string>(e.what()));
        }
        catch (...)
        {
            std::cout << "unknown exception" << std::endl;
            return MakeReadyFuture(std::optional<std::string>("unknown exception"));
        }
    };
}

static auto withSessionBinarySender(std::weak_ptr<brynet::net::TcpConnection> weakSession)
{
    return [weakSession = std::move(weakSession)](gayrpc::core::RpcMeta&& meta,
                                                  const google::protobuf::Message& message,
                                                  gayrpc::core::UnaryHandler&& next,
                                                  InterceptorContextType&& context) {
        gayrpc::protocol::binary::send(meta, message, weakSession);
        return next(std::move(meta),
                    message,
                    std::move(context));
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
        handleManager->handleRpcMsg(std::move(timeoutMeta),
                                    "",
                                    std::move(context));
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "handle rpc cause exception:" << e.what() << std::endl;
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
            eventLoop->runAfter(std::chrono::seconds(meta.request_info().timeout()),
                                [seqID = meta.request_info().sequence_id(), handleManager]() {
                                    causeTimeout(handleManager, seqID);
                                });
        }

        return next(std::move(meta),
                    message,
                    std::move(context));
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
        return next(std::move(meta),
                    message,
                    std::move(context));
    };
}

}}// namespace gayrpc::utils
