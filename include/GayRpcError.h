#ifndef _GAY_RPC_ERROR_H
#define _GAY_RPC_ERROR_H

#include <cstdint>
#include <string>

#include "meta.pb.h"

namespace gayrpc
{
    namespace core
    {
        typedef int32_t ErrorCode;

        // 封装RPC 远端返回的错误类型,rpc.call 本身的错误则由异常提供
        class RpcError
        {
        public:
            RpcError()
                :
                mFailed(false)
            {}

            RpcError(bool failed, 
                ErrorCode errorCode, 
                const std::string& reason)
                :
                mFailed(failed),
                mErrorCode(errorCode),
                mReason(reason)
            {}

            virtual ~RpcError()
            {}

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
        };
    }
}

#endif