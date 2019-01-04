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
        std::vector<UnaryServerInterceptor> interceptors = { args... };

        if (interceptors.empty())
        {
            return [](const RpcMeta& meta,
                const google::protobuf::Message& message,
                const UnaryHandler& next) {
                next(meta, message);
            };
        }
        else
        {
            auto lastIndex = interceptors.size() - 1;
            return [interceptors, lastIndex](const RpcMeta& meta,
                const google::protobuf::Message& message,
                const UnaryHandler& next) {
                size_t curI = 0;
                UnaryHandler magicHandler;
                magicHandler = [lastIndex, &curI, &interceptors, &next, &magicHandler](const RpcMeta& meta,
                    const google::protobuf::Message& message) {
                    if (curI == lastIndex)
                    {
                        return next(meta, message);
                    }
                    curI++;
                    return interceptors[curI](meta, message, magicHandler);
                };

                return interceptors[0](meta, message, magicHandler);
            };
        }
    }

} }
