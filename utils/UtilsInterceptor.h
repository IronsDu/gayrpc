#ifndef _GAY_RPC_UTILS_INTERCEPTOR_H
#define _GAY_RPC_UTILS_INTERCEPTOR_H

#include <functional>
#include <memory>
#include <exception>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"

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

    auto withSessionSender(std::weak_ptr<brynet::net::TCPSession> weakSession)
    {
        return [weakSession](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next) {
            sender(meta, message, next, weakSession);
        };
    }
}

#endif