#pragma once

#include <memory>

#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/GayRpcType.h>

namespace gayrpc { namespace core {

    class ServiceContext final
    {
    public:
        ServiceContext(RpcTypeHandleManager::PTR typeHandleManager, const UnaryServerInterceptor& inInterceptor, const UnaryServerInterceptor& outInterceptor)
            :
            mTypeHandleManager(typeHandleManager),
            mInInterceptor(inInterceptor),
            mOutInterceptor(outInterceptor)
        {}

        const RpcTypeHandleManager::PTR&    getTypeHandleManager() const
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
        const RpcTypeHandleManager::PTR     mTypeHandleManager;
        const UnaryServerInterceptor        mInInterceptor;
        const UnaryServerInterceptor        mOutInterceptor;
    };

    class BaseService : public std::enable_shared_from_this<BaseService>
    {
    public:
        explicit BaseService(ServiceContext&& context)
            :
            mContext(std::forward<ServiceContext>(context))
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

} }