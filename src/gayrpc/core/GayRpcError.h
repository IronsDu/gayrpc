#pragma once

#include <cstdint>
#include <string>

namespace gayrpc { namespace core {

    typedef int32_t ErrorCode;

    // 封装RPC 远端返回的错误类型,rpc.call 本身的错误则由异常提供
    class RpcError
    {
    public:
        RpcError()
            :
            mFailed(false),
            mErrorCode(0)
        {}

        RpcError(bool failed,
            ErrorCode errorCode,
            std::string reason)
            :
            mFailed(failed),
            mErrorCode(errorCode),
            mReason(std::move(reason))
        {}

        virtual ~RpcError() = default;

        void        setTimeout()
        {
            mTimeout = true;
        }

        bool        timeout() const
        {
            return mTimeout;
        }

        bool        failed() const
        {
            return mFailed;
        }

        ErrorCode   code() const
        {
            return mErrorCode;
        }

        const std::string& reason() const
        {
            return mReason;
        }

    private:
        bool        mFailed;
        ErrorCode   mErrorCode;
        std::string mReason;
        bool        mTimeout = false;
    };

} }