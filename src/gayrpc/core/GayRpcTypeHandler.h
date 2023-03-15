#pragma once

#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gayrpc::core {

// 管理不同类型RPC消息(REQUEST和Response)以及不同服务的处理器
class RpcTypeHandleManager : public std::enable_shared_from_this<RpcTypeHandleManager>
{
public:
    using Ptr = std::shared_ptr<RpcTypeHandleManager>;
    using ServiceHandler = std::function<void(RpcMeta&&, const std::string_view& body, InterceptorContextType&&)>;
    using ServiceHandlerMap = std::unordered_map<ServiceIDType, ServiceHandler>;

public:
    virtual ~RpcTypeHandleManager() = default;

    bool registerTypeHandle(RpcMeta::Type type, ServiceHandler&& handle, ServiceIDType serviceID)
    {
        std::unique_lock<std::shared_mutex> lock(mMutex);
        auto& serviceMap = mTypeHandlers[type];
        if (serviceMap.find(serviceID) != serviceMap.end())
        {
            return false;
        }
        serviceMap[serviceID] = std::move(handle);
        return true;
    }

    void removeTypeHandle(RpcMeta::Type type, ServiceIDType serviceID)
    {
        std::unique_lock<std::shared_mutex> lock(mMutex);
        if (mTypeHandlers.find(type) == mTypeHandlers.end())
        {
            return;
        }
        mTypeHandlers[type].erase(serviceID);
        if (mTypeHandlers[type].empty())
        {
            mTypeHandlers.erase(type);
        }
    }

    void removeAll()
    {
        std::unique_lock<std::shared_mutex> lock(mMutex);
        mTypeHandlers.clear();
    }

    void handleRpcMsg(RpcMeta&& meta, const std::string_view& data, InterceptorContextType&& context) const
    {
        std::shared_lock<std::shared_mutex> lock(mMutex);

        const auto it = mTypeHandlers.find(meta.type());
        if (it == mTypeHandlers.end())
        {
            throw std::runtime_error("not found type handle of type:" + std::to_string(meta.type()));
        }

        const auto& serviceMap = (*it).second;
        const auto serviceIt = serviceMap.find(meta.service_id());
        if (serviceIt == serviceMap.end())
        {
            throw std::runtime_error("not found service handle of id:" + std::to_string(meta.service_id()));
        }

        (serviceIt->second)(std::move(meta), data, std::move(context));
    }

private:
    std::map<RpcMeta::Type, ServiceHandlerMap> mTypeHandlers;
    mutable std::shared_mutex mMutex;
};

}// namespace gayrpc::core
