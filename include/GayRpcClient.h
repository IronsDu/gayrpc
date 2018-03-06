#ifndef _GAY_RPC_CLIENT_H
#define _GAY_RPC_CLIENT_H

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>

#include "meta.pb.h"
#include "GayRpcTypeHandler.h"
#include "GayRpcHelper.h"

namespace gayrpc
{
    namespace core
    {
        class BaseClient : public std::enable_shared_from_this<BaseClient>
        {
        public:
            typedef std::shared_ptr<BaseClient> PTR;

        protected:
            BaseClient(UnaryServerInterceptor outboundInterceptor,
                UnaryServerInterceptor inboundInterceptor)
                :
                mSequenceID(0),
                mOutboundInterceptor(std::move(outboundInterceptor)),
                mInboundInterceptor(std::move(inboundInterceptor))
            {}

            template<typename Response, typename Request, typename Handle>
            void call(const Request& request,
                uint64_t msgID,
                const Handle& handle = nullptr)
            {
                const auto sequenceID = mSequenceID++;
                const auto expectResponse = (handle != nullptr);

                RpcMeta meta = makeRequestRpcMeta(sequenceID,
                    msgID,
                    RpcMeta_DataEncodingType_BINARY,
                    handle != nullptr);
                mOutboundInterceptor(meta, request, [](const RpcMeta&, const google::protobuf::Message&) {
                });

                if (!expectResponse)
                {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lck(mStubMapGruad);
                    auto sharedThis = shared_from_this();
                    mStubHandleMap[sequenceID] = [handle, sharedThis](const RpcMeta& meta,
                        const std::string& data,
                        const UnaryServerInterceptor& inboundInterceptor) {
                        return parseWrapper<Response>(handle,
                            meta,
                            data,
                            inboundInterceptor);
                    };
                }
            }

            void    installResponseStub(const gayrpc::core::RpcTypeHandleManager::PTR& rpcTypeHandleManager)
            {
                auto sharedThis = shared_from_this();
                auto responseStub = [sharedThis](const RpcMeta& meta,
                    const std::string& data) {
                    sharedThis->processRpcResponse(meta, data);
                    return true;
                };
                rpcTypeHandleManager->registerTypeHandle(RpcMeta::RESPONSE, responseStub);
            }

        private:
            void    processRpcResponse(const RpcMeta& meta, const std::string& data)
            {
                assert(meta.type() == RpcMeta::RESPONSE);
                if (meta.type() != RpcMeta::RESPONSE)
                {
                    throw std::runtime_error("type :" + std::to_string(meta.type()) + " not Response");
                }

                ResponseStubHandle handle;
                {
                    std::lock_guard<std::mutex> lck(mStubMapGruad);
                    auto it = mStubHandleMap.find(meta.response_info().sequence_id());
                    if (it == mStubHandleMap.end())
                    {
                        throw std::runtime_error("not found response seq id:" +
                            std::to_string(meta.response_info().sequence_id()));
                    }
                    handle = (*it).second;
                    mStubHandleMap.erase(it);
                }
                handle(meta, data, mInboundInterceptor);
            }

        private:
            typedef std::function<
                void(
                    const RpcMeta&, 
                    const std::string& data, 
                    const UnaryServerInterceptor&)> ResponseStubHandle;
            typedef std::unordered_map<uint64_t, ResponseStubHandle> ResponseStubHandleMap;

            UnaryServerInterceptor      mInboundInterceptor;
            UnaryServerInterceptor      mOutboundInterceptor;

            std::atomic<uint64_t>        mSequenceID;
            std::mutex                  mStubMapGruad;
            ResponseStubHandleMap       mStubHandleMap;
        };
    }
}

#endif
