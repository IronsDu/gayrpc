#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/GayRpcHelper.h>

namespace gayrpc { namespace core {

    class BaseClient : public std::enable_shared_from_this<BaseClient>
    {
    public:
        using PTR = std::shared_ptr<BaseClient>;
        using TIMEOUT_CALLBACK = std::function<void(void)>;

    public:
        const RpcTypeHandleManager::PTR&    getTypeHandleManager() const
        {
            return mTypeHandleManager;
        }

        const UnaryServerInterceptor&       getInInterceptor() const
        {
            return mInboundInterceptor;
        }

        const UnaryServerInterceptor&       getOutInterceptor() const
        {
            return mOutboundInterceptor;
        }

    protected:
        BaseClient(RpcTypeHandleManager::PTR rpcHandlerManager,
            UnaryServerInterceptor inboundInterceptor,
            UnaryServerInterceptor outboundInterceptor)
            :
            mTypeHandleManager(std::move(rpcHandlerManager)),
            mInboundInterceptor(std::move(inboundInterceptor)),
            mOutboundInterceptor(std::move(outboundInterceptor)),
            mSequenceID(0)
        {}

        virtual ~BaseClient() = default;

        template<typename Response, typename Request, typename Handle>
        void call(const Request& request,
            ServiceIDType serviceID,
            ServiceFunctionMsgIDType msgID,
            const Handle& handle,
            std::chrono::seconds timeout,
            TIMEOUT_CALLBACK timeoutCallback)
        {
            const auto sequenceID = mSequenceID++;

            RpcMeta meta = makeRequestRpcMeta(sequenceID,
                serviceID,
                msgID,
                RpcMeta_DataEncodingType_BINARY,
                true);
            meta.mutable_request_info()->set_timeout(static_cast<uint64_t>(timeout.count()));

            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);
                assert(mStubHandleMap.find(sequenceID) == mStubHandleMap.end());
                assert(mTimeoutHandleMap.find(sequenceID) == mTimeoutHandleMap.end());

                mStubHandleMap[sequenceID] = [handle](const RpcMeta & meta,
                    const std::string_view & data,
                    const UnaryServerInterceptor & inboundInterceptor,
                    InterceptorContextType context) {
                        return parseResponseWrapper<Response>(handle,
                            meta,
                            data,
                            inboundInterceptor,
                            std::move(context));
                };
                mTimeoutHandleMap[sequenceID] = std::move(timeoutCallback);
            }

            InterceptorContextType context;
            mOutboundInterceptor(meta, request, [](const RpcMeta&, const google::protobuf::Message&, InterceptorContextType context) {
            }, std::move(context));
        }

        template<typename Response, typename Request, typename Handle>
        void call(const Request& request,
            ServiceIDType serviceID,
            ServiceFunctionMsgIDType msgID,
            const Handle& handle = nullptr)
        {
            const auto sequenceID = mSequenceID++;
            const auto expectResponse = (handle != nullptr);

            RpcMeta meta = makeRequestRpcMeta(sequenceID,
                serviceID,
                msgID,
                RpcMeta_DataEncodingType_BINARY,
                expectResponse);
            meta.mutable_request_info()->set_timeout(0);

            if (expectResponse)
            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);
                assert(mStubHandleMap.find(sequenceID) == mStubHandleMap.end());

                mStubHandleMap[sequenceID] = [handle](const RpcMeta & meta,
                    const std::string_view & data,
                    const UnaryServerInterceptor & inboundInterceptor,
                    InterceptorContextType context) {
                        return parseResponseWrapper<Response>(handle,
                            meta,
                            data,
                            inboundInterceptor,
                            std::move(context));
                };
            }

            InterceptorContextType context;
            mOutboundInterceptor(meta, request, [](const RpcMeta&, const google::protobuf::Message&, InterceptorContextType context) {
            }, std::move(context));
        }

        void    installResponseStub(const gayrpc::core::RpcTypeHandleManager::PTR& rpcTypeHandleManager,
            ServiceIDType serviceID)
        {
            auto sharedThis = shared_from_this();
            auto responseStub = [sharedThis](const RpcMeta& meta,
                const std::string_view & data,
                InterceptorContextType context) {
                sharedThis->processRpcResponse(meta, data, std::move(context));
                return true;
            };
            rpcTypeHandleManager->registerTypeHandle(RpcMeta::RESPONSE, responseStub, serviceID);
        }

    private:
        void    processRpcResponse(const RpcMeta& meta, const std::string_view& data, InterceptorContextType context)
        {
            assert(meta.type() == RpcMeta::RESPONSE);
            if (meta.type() != RpcMeta::RESPONSE)
            {
                throw std::runtime_error("type :" + std::to_string(meta.type()) + " not Response");
            }

            if (meta.response_info().timeout())
            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);

                mStubHandleMap.erase(meta.response_info().sequence_id());

                auto it = mTimeoutHandleMap.find(meta.response_info().sequence_id());
                if (it == mTimeoutHandleMap.end())
                {
                    return;
                }
                (*it).second();
                mTimeoutHandleMap.erase(it);

                return;
            }

            ResponseStubHandle handle;
            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);

                mTimeoutHandleMap.erase(meta.response_info().sequence_id());

                auto it = mStubHandleMap.find(meta.response_info().sequence_id());
                if (it == mStubHandleMap.end())
                {
                    throw std::runtime_error("not found response seq id:" +
                        std::to_string(meta.response_info().sequence_id()));
                }
                handle = (*it).second;
                mStubHandleMap.erase(it);
            }
            handle(meta, data, mInboundInterceptor, std::move(context));
        }

    private:
        using ResponseStubHandle =
            std::function<
            void(
                const RpcMeta&,
                const std::string_view& data,
                const UnaryServerInterceptor&,
                InterceptorContextType context)>;
        using ResponseStubHandleMap = std::unordered_map<uint64_t, ResponseStubHandle>;
        using TimeoutHandleMap = std::unordered_map<uint64_t, TIMEOUT_CALLBACK>;

        const RpcTypeHandleManager::PTR     mTypeHandleManager;
        const UnaryServerInterceptor        mInboundInterceptor;
        const UnaryServerInterceptor        mOutboundInterceptor;

        std::atomic<uint64_t>               mSequenceID;
        std::mutex                          mStubMapGruad;
        ResponseStubHandleMap               mStubHandleMap;
        TimeoutHandleMap                    mTimeoutHandleMap;
    };

} }
