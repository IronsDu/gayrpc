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
        return [=](RpcMeta&& meta,
            const google::protobuf::Message& message,
            UnaryHandler&& tail,
            InterceptorContextType&& context) {

                class WrapNextInterceptor final
                {
                public:
                    WrapNextInterceptor(std::vector<UnaryServerInterceptor> interceptors,
                        UnaryHandler&& tail) noexcept
                        :
                        mCurIndex(0),
                        mInterceptors(std::move(interceptors)),
                        mTail(std::forward<UnaryHandler>(tail))
                    {
                    }

                    void operator () (RpcMeta&& meta,
                        const google::protobuf::Message& message,
                        InterceptorContextType&& context)
                    {
                        if (mInterceptors.size() == mCurIndex)
                        {
                            return mTail(std::forward<RpcMeta>(meta), message, std::forward<InterceptorContextType>(context));
                        }
                        else
                        {
                            mCurIndex++;
                            mInterceptors[mCurIndex - 1](std::forward<RpcMeta>(meta), message, *this, std::forward<InterceptorContextType>(context));
                        }
                    }

                    size_t  curIndex() const
                    {
                        return mCurIndex;
                    }

                private:
                    size_t  mCurIndex;
                    const std::vector<UnaryServerInterceptor> mInterceptors;
                    const UnaryHandler    mTail;
                };

                WrapNextInterceptor next({ args... }, std::forward<UnaryHandler>(tail));
                return next(std::move(meta), message, std::forward<InterceptorContextType>(context));
        };
    }

} }
