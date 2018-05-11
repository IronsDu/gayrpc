#ifndef _GAY_RPC_TYPE_HANDLER_H
#define _GAY_RPC_TYPE_HANDLER_H

#include <string>
#include <unordered_map>
#include <map>
#include <functional>
#include <memory>
#include <shared_mutex>

#include "meta.pb.h"

namespace gayrpc
{
    namespace core
    {
        // 管理不同类型RPC消息(REQUEST和Response)以及不同服务的处理器
        class RpcTypeHandleManager : public std::enable_shared_from_this<RpcTypeHandleManager>
        {
        public:
            typedef std::shared_ptr<RpcTypeHandleManager> PTR;
            typedef std::function<bool(const RpcMeta&, const std::string& body)>    ServiceHandler;
            typedef std::unordered_map<int, ServiceHandler>                         ServiceHandlerMap;

        public:
            bool    registerTypeHandle(RpcMeta::Type type, ServiceHandler handle, int serviceID)
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

            void    removeTypeHandle(RpcMeta::Type type, int serviceID)
            {
                std::unique_lock<std::shared_mutex> lock(mMutex);
                if (mTypeHandlers.find(type) == mTypeHandlers.end())
                {
                    return;
                }
                mTypeHandlers[type].erase(serviceID);
            }

            virtual ~RpcTypeHandleManager()
            {
            }

            bool    handleRpcMsg(const RpcMeta& meta,
                const std::string& data)
            {
                ServiceHandler handler;
                {
                    std::shared_lock<std::shared_mutex> lock(mMutex);
                    auto it = mTypeHandlers.find(meta.type());
                    if (it == mTypeHandlers.end())
                    {
                        return false;
                    }
                    auto& serviceMap = (*it).second;
                    auto serviceIt = serviceMap.find(meta.service_id());
                    if (serviceIt == serviceMap.end())
                    {
                        return false;
                    }
                    handler = (*serviceIt).second;
                }

                return handler(meta, data);
            }

        private:
            std::map<int, ServiceHandlerMap>            mTypeHandlers;
            std::shared_mutex                           mMutex;
        };
    }
}

#endif
