#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>

namespace gayrpc { namespace core {

    template<typename... Interceptors>
    UnaryServerInterceptor makeInterceptor(Interceptors... interceptors)
    {
        std::vector<UnaryServerInterceptor> userInterceptors{ interceptors... };

        UnaryServerInterceptor combinationInterceptor = [](
                gayrpc::core::RpcMeta&& meta,
                const google::protobuf::Message& message,
                UnaryHandler&& next,
                InterceptorContextType&& context)
        {
            return next(std::move(meta), message, std::move(context));
        };

        for(auto it = userInterceptors.crbegin(); it != userInterceptors.crend(); it++)
        {
            auto wrapper = [userInterceptor = *it](UnaryServerInterceptor nextInterceptor)
            {
                return [=](
                        gayrpc::core::RpcMeta&& meta,
                        const google::protobuf::Message& message,
                        UnaryHandler&& next,
                        InterceptorContextType&& context) mutable
                {
                    return userInterceptor(
                                std::move(meta),
                                message,
                                [=, next = std::move(next)](gayrpc::core::RpcMeta&& meta,
                                    const google::protobuf::Message& message,
                                    InterceptorContextType&& context) mutable
                                {
                                    return nextInterceptor(std::move(meta),
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

} }
