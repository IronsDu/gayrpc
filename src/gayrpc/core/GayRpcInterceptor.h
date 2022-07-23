#pragma once

#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

#include <functional>
#include <memory>
#include <vector>

namespace gayrpc::core {

template<typename... Interceptors>
UnaryServerInterceptor makeInterceptor(Interceptors... interceptors)
{
    const std::vector<UnaryServerInterceptor> userInterceptors{interceptors...};

    UnaryServerInterceptor combinationInterceptor =
            [](gayrpc::core::RpcMeta&& meta,
               const google::protobuf::Message& message,
               UnaryHandler&& next,
               InterceptorContextType&& context) {
                return next(std::move(meta), message, std::move(context));
            };

    for (auto it = userInterceptors.crbegin(); it != userInterceptors.crend(); ++it)
    {
        auto wrapper = [userInterceptor = *it](UnaryServerInterceptor nextInterceptor) mutable {
            return [nextInterceptorPtr = std::make_shared<UnaryServerInterceptor>(nextInterceptor),
                    userInterceptor = std::move(userInterceptor)](
                           gayrpc::core::RpcMeta&& meta,
                           const google::protobuf::Message& message,
                           UnaryHandler&& next,
                           InterceptorContextType&& context) mutable {
                return userInterceptor(
                        std::move(meta),
                        message,
                        [nextInterceptorPtr, next = std::move(next)](gayrpc::core::RpcMeta&& meta,
                                                                     const google::protobuf::Message& message,
                                                                     InterceptorContextType&& context) mutable {
                            return (*nextInterceptorPtr)(std::move(meta),
                                                         message,
                                                         std::move(next),
                                                         std::move(context));
                        },
                        std::move(context));
            };
        };
        combinationInterceptor = wrapper(combinationInterceptor);
    }

    return combinationInterceptor;
}

}// namespace gayrpc::core
