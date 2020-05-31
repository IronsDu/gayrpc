#pragma once

#include <memory>
#include <utility>

#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/GayRpcType.h>

namespace gayrpc::core {

    class ServiceContext final
    {
    public:
        ServiceContext(RpcTypeHandleManager::Ptr typeHandleManager,
                UnaryServerInterceptor  inInterceptor,
                UnaryServerInterceptor  outInterceptor)
            :
            mTypeHandleManager(std::move(typeHandleManager)),
            mInInterceptor(std::move(inInterceptor)),
            mOutInterceptor(std::move(outInterceptor))
        {}

        const RpcTypeHandleManager::Ptr&    getTypeHandleManager() const
        {
            return mTypeHandleManager;
        }

        const UnaryServerInterceptor&       getInInterceptor() const
        {
            return mInInterceptor;
        }

        const UnaryServerInterceptor&       getOutInterceptor() const
        {
            return mOutInterceptor;
        }

    private:
        const RpcTypeHandleManager::Ptr     mTypeHandleManager;
        const UnaryServerInterceptor        mInInterceptor;
        const UnaryServerInterceptor        mOutInterceptor;
    };

    class BaseService : public std::enable_shared_from_this<BaseService>
    {
    public:
        explicit BaseService(ServiceContext&& context)
            :
            mContext(std::move(context))
        {
        }
        virtual ~BaseService() = default;

        virtual void onClose() {}

        const ServiceContext&               getServiceContext() const
        {
            return mContext;
        }

        virtual void    install() = 0;

    private:
        const ServiceContext                mContext;
    };

}