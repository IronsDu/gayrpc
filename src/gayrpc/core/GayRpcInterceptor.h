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
        return [=](RpcMeta&& meta,
            const google::protobuf::Message& message,
            UnaryHandler&& tail,
            InterceptorContextType&& context) {

                class WrapNextInterceptor final
                {
                public:
                    WrapNextInterceptor(std::vector<UnaryServerInterceptor>&& interceptors,
                        UnaryHandler&& tail) noexcept
                        :
                        mCurIndex(0),
                        mInterceptors(std::move(interceptors)),
                        mTail(std::forward<UnaryHandler>(tail))
                    {
                    }

                    InterceptorReturnType operator () (RpcMeta&& meta,
                        const google::protobuf::Message& message,
                        InterceptorContextType&& context)
                    {
                        if (mInterceptors.size() == mCurIndex)
                        {
                            return mTail(std::forward<RpcMeta>(meta), 
                                message, 
                                std::forward<InterceptorContextType>(context));
                        }
                        else
                        {
                            mCurIndex++;
                            auto interceptor = std::move(mInterceptors[mCurIndex - 1]);
                            auto result = interceptor(std::forward<RpcMeta>(meta),
                                message, 
                                std::move(*this), 
                                std::forward<InterceptorContextType>(context));
                            // expect the std::move(*this) is really successful;
                            assert(mTail == nullptr);
                            assert(mInterceptors.empty());
                            return result;
                        }
                    }

                private:
                    size_t                              mCurIndex;
                    std::vector<UnaryServerInterceptor> mInterceptors;
                    UnaryHandler                        mTail;
                };

                WrapNextInterceptor next(std::vector<UnaryServerInterceptor>{ interceptors... },
                    std::forward<UnaryHandler>(tail));
                return next(std::move(meta), message, std::forward<InterceptorContextType>(context));
        };
    }

} }
