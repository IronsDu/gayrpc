#ifndef _GAY_RPC_SERVICE_H
#define _GAY_RPC_SERVICE_H

#include <memory>

#include "GayRpcTypeHandler.h"
#include "GayRpcCore.h"

namespace gayrpc
{
    namespace core
    {
        class ServiceContext final
        {
        public:
            ServiceContext(RpcTypeHandleManager::PTR typeHandleManager, UnaryServerInterceptor inInterceptor, UnaryServerInterceptor outInterceptor)
                :
                mTypeHandleManager(typeHandleManager),
                mInInterceptor(inInterceptor),
                mOutInterceptor(outInterceptor)
            {}

            RpcTypeHandleManager::PTR   getTypeHandleManager() const
            {
                return mTypeHandleManager;
            }

            UnaryServerInterceptor      getInInterceptor() const
            {
                return mInInterceptor;
            }

            UnaryServerInterceptor      getOutInterceptor() const
            {
                return mOutInterceptor;
            }

        private:
            const RpcTypeHandleManager::PTR mTypeHandleManager;
            const UnaryServerInterceptor    mInInterceptor;
            const UnaryServerInterceptor    mOutInterceptor;
        };

        class BaseService : public std::enable_shared_from_this<BaseService>
        {
        public:
            BaseService(ServiceContext context)
                :
                mContext(context)
            {
            }
            virtual ~BaseService()
            {}

            virtual void onClose() {}

            const ServiceContext&       getServiceContext() const
            {
                return mContext;
            }

        private:
            const ServiceContext        mContext;
        };
    }
}

#endif