#pragma once

#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcTypeHandler.h>

#include <memory>
#include <utility>

namespace gayrpc::core {

class ServiceContext final
{
public:
    ServiceContext(RpcTypeHandleManager::Ptr typeHandleManager,
                   UnaryServerInterceptor inInterceptor,
                   UnaryServerInterceptor outInterceptor)
        : mTypeHandleManager(std::move(typeHandleManager)),
          mInInterceptor(std::move(inInterceptor)),
          mOutInterceptor(std::move(outInterceptor))
    {}

    [[nodiscard]] const RpcTypeHandleManager::Ptr& getTypeHandleManager() const
    {
        return mTypeHandleManager;
    }

    [[nodiscard]] const UnaryServerInterceptor& getInInterceptor() const
    {
        return mInInterceptor;
    }

    [[nodiscard]] const UnaryServerInterceptor& getOutInterceptor() const
    {
        return mOutInterceptor;
    }

private:
    const RpcTypeHandleManager::Ptr mTypeHandleManager;
    const UnaryServerInterceptor mInInterceptor;
    const UnaryServerInterceptor mOutInterceptor;
};

class BaseService : public std::enable_shared_from_this<BaseService>
{
public:
    explicit BaseService(ServiceContext&& context)
        : mContext(std::move(context))
    {
    }
    virtual ~BaseService() = default;

    const ServiceContext& getServiceContext() const
    {
        return mContext;
    }

    virtual void onClose()
    {}

    virtual void uninstall() = 0;

    virtual void install() = 0;

private:
    const ServiceContext mContext;
};

}// namespace gayrpc::core
