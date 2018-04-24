#ifndef _GAY_RPC_UTILS_INTERCEPTOR_H
#define _GAY_RPC_UTILS_INTERCEPTOR_H

#include <functional>
#include <memory>
#include <exception>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"
#include "GayRpcTypeHandler.h"

namespace utils_interceptor
{
    // 一些辅助型拦截器

    auto withProtectedCall()
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

    auto withSessionSender(brynet::net::TCPSession::WEAK_PTR weakSession)
    {
        return [weakSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {
            sender(meta, message, next, weakSession);
        };
    }

    static void causeTimeout(const gayrpc::core::RpcTypeHandleManager::PTR& handleManager,
        uint64_t seq_id)
    {
        gayrpc::core::RpcMeta timeoutMeta;
        timeoutMeta.set_type(gayrpc::core::RpcMeta::RESPONSE);
        timeoutMeta.mutable_response_info()->set_timeout(true);
        timeoutMeta.mutable_response_info()->set_sequence_id(seq_id);
        handleManager->handleRpcMsg(timeoutMeta, "");
    }

    // 由eventLoop线程处理超时检测
    auto withTimeoutCheck(const brynet::net::EventLoop::PTR& eventLoop,
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
}

#endif