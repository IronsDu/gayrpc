#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <map>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <any>

#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

namespace gayrpc::core {

    // 管理不同类型RPC消息(REQUEST和Response)以及不同服务的处理器
    class RpcTypeHandleManager : public std::enable_shared_from_this<RpcTypeHandleManager>
    {
    public:
        using Ptr = std::shared_ptr<RpcTypeHandleManager>;
        using ServiceHandler = std::function<void(RpcMeta&&, const std::string_view& body, InterceptorContextType&&)>;
        using ServiceHandlerMap = std::unordered_map<ServiceIDType, ServiceHandler>;

    public:
        bool    registerTypeHandle(RpcMeta::Type type, ServiceHandler&& handle, ServiceIDType serviceID)
        {
            std::unique_lock<std::shared_mutex> lock(mMutex);
            auto& serviceMap = mTypeHandlers[type];
            if (serviceMap.find(serviceID) != serviceMap.end())
            {
                return false;
            }
            serviceMap[serviceID] = std::forward<ServiceHandler>(handle);
            return true;
        }

        void    removeTypeHandle(RpcMeta::Type type, ServiceIDType serviceID)
        {
            std::unique_lock<std::shared_mutex> lock(mMutex);
            if (mTypeHandlers.find(type) == mTypeHandlers.end())
            {
                return;
            }
            mTypeHandlers[type].erase(serviceID);
        }

        virtual ~RpcTypeHandleManager() = default;

        void    handleRpcMsg(RpcMeta&& meta, const std::string_view & data, InterceptorContextType&& context)
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);

            const auto it = mTypeHandlers.find(meta.type());
            if (it == mTypeHandlers.end())
            {
                throw std::runtime_error("not found type handle of type:"
                                         + std::to_string(meta.type()));
            }

            const auto& serviceMap = (*it).second;
            const auto serviceIt = serviceMap.find(meta.service_id());
            if (serviceIt == serviceMap.end())
            {
                throw std::runtime_error("not found service handle of id:"
                                         + std::to_string(meta.service_id()));
            }

            (serviceIt->second)(std::forward<RpcMeta>(meta), data, std::forward<InterceptorContextType>(context));
        }

    private:
        std::map<RpcMeta::Type, ServiceHandlerMap>  mTypeHandlers;
        std::shared_mutex                           mMutex;
    };

}
