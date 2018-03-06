#ifndef _GAY_RPC_TYPE_HANDLER_H
#define _GAY_RPC_TYPE_HANDLER_H

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>

#include "meta.pb.h"

namespace gayrpc
{
    namespace core
    {
        // 管理不同类型RPC消息(REQUEST和Response)的处理器
        class RpcTypeHandleManager : public std::enable_shared_from_this<RpcTypeHandleManager>
        {
        public:
            typedef std::shared_ptr<RpcTypeHandleManager> PTR;
            typedef std::function<bool(const RpcMeta&, const std::string& body)> TypeHandler;

        public:
            void    registerTypeHandle(RpcMeta::Type type, TypeHandler handle)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mTypeHandlers[type] = std::move(handle);
            }

            virtual ~RpcTypeHandleManager()
            {
            }

            bool    handleRpcMsg(const RpcMeta& meta,
                const std::string& data)
            {
                TypeHandler handler;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    auto it = mTypeHandlers.find(meta.type());
                    if (it == mTypeHandlers.end())
                    {
                        return false;
                    }
                    handler = (*it).second;
                }

                try
                {
                    handler(meta, data);
                }
                catch (const std::exception& e)
                {
                    std::cout << e.what() << std::endl;
                }
                return true;
            }

        private:
            std::unordered_map<int, TypeHandler>  mTypeHandlers;
            std::mutex                                      mMutex;
        };
    }
}

#endif
