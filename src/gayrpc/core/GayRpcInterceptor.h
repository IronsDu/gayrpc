#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>

namespace gayrpc { namespace core {

    template<class... Args>
    UnaryServerInterceptor makeInterceptor(Args... args)
    {
        if (sizeof...(Args) == 0)
        {
            return [](const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next, InterceptorContextType context) {
                next(meta, message, std::move(context));
            };
        }
        else
        {
            using InterceptorList = std::vector<UnaryServerInterceptor>;
            std::shared_ptr<InterceptorList> interceptors = std::make_shared<InterceptorList>(InterceptorList{ args... });
            auto lastIndex = interceptors->size() - 1;

            return [=](const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next, InterceptorContextType context) {

                std::shared_ptr<size_t> curIndex = std::make_shared<size_t>(0);
                std::shared_ptr<UnaryHandler> magicHandler = std::make_shared<UnaryHandler>();

                *magicHandler = [=](const RpcMeta& meta, const google::protobuf::Message& message, InterceptorContextType context) {
                    if (*curIndex == lastIndex)
                    {
                        return next(meta, message, std::move(context));
                    }
                    (*curIndex)++;
                    return (*interceptors)[*curIndex](meta, message, *magicHandler, std::move(context));
                };

                return (*interceptors)[0](meta, message, *magicHandler, std::move(context));
            };
        }
    }

} }
