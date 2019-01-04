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

    // 一些辅助型拦截器

    static auto withProtectedCall()
    {
        return [](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {
            try
            {
                next(meta, message);
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

    static auto withSessionBinarySender(std::weak_ptr<brynet::net::DataSocket> weakSession)
    {
        return [weakSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {
            gayrpc::protocol::binary::send(meta, message, next, weakSession);
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
            handleManager->handleRpcMsg(timeoutMeta, "");
        }
        catch (...)
        {
        }
    }

    // 由eventLoop线程处理超时检测
    static auto withTimeoutCheck(const brynet::net::EventLoop::PTR& eventLoop,
        const gayrpc::core::RpcTypeHandleManager::PTR& handleManager)
    {
        return [eventLoop, handleManager](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {

            if (meta.request_info().timeout() > 0)
            {
                auto seqID = meta.request_info().sequence_id();
                auto timeoutSecond = meta.request_info().timeout();

                eventLoop->pushAsyncProc([eventLoop, seqID, timeoutSecond, handleManager]() {
                    eventLoop->getTimerMgr()->addTimer(std::chrono::seconds(timeoutSecond),
                        [handleManager, seqID]() {
                        causeTimeout(handleManager, seqID);
                    });
                });
            }

            next(meta, message);
        };
    }

    static auto withHttpSessionSender(const brynet::net::http::HttpSession::PTR& httpSession)
    {
        return [httpSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {
            gayrpc::protocol::http::send(meta, message, httpSession);
            httpSession->postShutdown();
            next(meta, message);
        };
    }

} }