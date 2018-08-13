#ifndef _GAY_RPC_INTERCEPTOR_H
#define _GAY_RPC_INTERCEPTOR_H

#include <functional>
#include <memory>
#include <exception>
#include <vector>

#include "meta.pb.h"
#include "GayRpcCore.h"

namespace gayrpc
{
    namespace utils
    {
        // 实现拦截器
        static auto togetherInterceptor(std::vector<core::UnaryServerInterceptor>& interceptorArray)
        {
        }
        template<class T>
        auto togetherInterceptor(std::vector<core::UnaryServerInterceptor>& interceptorArray, T handler)
        {
            interceptorArray.push_back(std::forward<T>(handler));
        }

        template<class T, class... Args>
        auto togetherInterceptor(std::vector<core::UnaryServerInterceptor>& interceptorArray, T headHandler, Args... leftHandlers)
        {
            interceptorArray.push_back(std::forward<T>(headHandler));
            togetherInterceptor(interceptorArray, std::forward<Args>(leftHandlers)...);
        }

        template<class... Args>
        core::UnaryServerInterceptor makeInterceptor(Args... args)
        {
            std::vector<core::UnaryServerInterceptor> interceptors;
            togetherInterceptor(interceptors, args...);

            auto n = interceptors.size();
            if (n > 1)
            {
                auto lastIndex = n - 1;
                return [interceptors, lastIndex](const gayrpc::core::RpcMeta& meta,
                    const google::protobuf::Message& message, 
                    const core::UnaryHandler& next) {
                    size_t curI = 0;
                    core::UnaryHandler fuck;
                    fuck = [lastIndex, &curI, &interceptors, &next, &fuck](const gayrpc::core::RpcMeta& meta,
                        const google::protobuf::Message& message) {
                        if (curI == lastIndex)
                        {
                            return next(meta, message);
                        }
                        curI++;
                        return interceptors[curI](meta, message, fuck);
                    };

                    return interceptors[0](meta, message, fuck);
                };

            }
            else if (n == 1)
            {
                return interceptors[0];
            }
            else
            {
                return [](const gayrpc::core::RpcMeta& meta,
                    const google::protobuf::Message& message, 
                    const core::UnaryHandler& next) {
                        next(meta, message);
                    };
            }
        }
    }
}

#endif
