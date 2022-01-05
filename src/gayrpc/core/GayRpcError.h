#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gayrpc::core {

typedef int32_t ErrorCode;

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

    RpcError(const RpcError& other)
        : mErrorCode(other.mErrorCode),
          mReason(other.mReason)
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
