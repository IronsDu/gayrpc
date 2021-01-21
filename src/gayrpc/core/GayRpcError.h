#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gayrpc::core {

typedef int32_t ErrorCode;

// 封装RPC 远端返回的错误类型,rpc.call 本身的错误则由异常提供
class RpcError
{
public:
    RpcError()
        : mErrorCode(0)
    {}

    RpcError(ErrorCode errorCode,
             std::string reason)
        : mErrorCode(errorCode),
          mReason(std::move(reason))
    {}

    virtual ~RpcError() = default;

    void setTimeout()
    {
        mTimeout = true;
    }

    bool timeout() const
    {
        return mTimeout;
    }

    ErrorCode code() const
    {
        return mErrorCode;
    }

    const std::string& reason() const
    {
        return mReason;
    }

private:
    ErrorCode mErrorCode;
    std::string mReason;
    bool mTimeout = false;
};

}// namespace gayrpc::core
