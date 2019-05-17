#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <queue>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/GayRpcHelper.h>

namespace gayrpc { namespace core {

    class WaitResponseTimer final
    {
    public:
        WaitResponseTimer(uint64_t seqID, std::chrono::nanoseconds lastTime)
            :
            mEndTime(std::chrono::steady_clock::now() + lastTime),
            mSeqID(seqID)
        {
        }

        const std::chrono::steady_clock::time_point& getEndTime() const
        {
            return mEndTime;
        }

        uint64_t    getSeqID() const
        {
            return mSeqID;
        }

    private:
        std::chrono::steady_clock::time_point   mEndTime;
        uint64_t                                mSeqID;
    };

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

        void    checkTimeout()
        {
            std::vector< TIMEOUT_CALLBACK>  callbacks;
            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);
                const auto now = std::chrono::steady_clock::now();
                while (!mWaitResponseTimerQueue.empty())
                {
                    const auto& head = mWaitResponseTimerQueue.top();
                    if (head.getEndTime() > now)
                    {
                        break;
                    }
                    auto it = mTimeoutHandleMap.find(head.getSeqID());
                    if (it != mTimeoutHandleMap.end())
                    {
                        callbacks.push_back((*it).second);
                        mTimeoutHandleMap.erase(head.getSeqID());
                        mStubHandleMap.erase(head.getSeqID());
                    }
                    mWaitResponseTimerQueue.pop();
                }
            }
            for (auto& callback : callbacks)
            {
                callback();
            }
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
                WaitResponseTimer waitTimer(sequenceID, timeout);
                auto callback = [handle](const RpcMeta & meta,
                    const std::string_view & data,
                    const UnaryServerInterceptor & inboundInterceptor,
                    InterceptorContextType context) {
                        return parseResponseWrapper<Response>(handle,
                            meta,
                            data,
                            inboundInterceptor,
                            std::move(context));
                };

                std::lock_guard<std::mutex> lck(mStubMapGruad);
                assert(mStubHandleMap.find(sequenceID) == mStubHandleMap.end());
                assert(mTimeoutHandleMap.find(sequenceID) == mTimeoutHandleMap.end());

                mStubHandleMap[sequenceID] = std::move(callback);
                mTimeoutHandleMap[sequenceID] = std::move(timeoutCallback);
                mWaitResponseTimerQueue.push(waitTimer);
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

            //TODO::remove it's mWaitResponseTimerQueue
            const auto sequenceID = meta.response_info().sequence_id();
            if (meta.response_info().timeout())
            {
                std::lock_guard<std::mutex> lck(mStubMapGruad);

                mStubHandleMap.erase(sequenceID);

                auto it = mTimeoutHandleMap.find(sequenceID);
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

                mTimeoutHandleMap.erase(sequenceID);

                auto it = mStubHandleMap.find(sequenceID);
                if (it == mStubHandleMap.end())
                {
                    throw std::runtime_error("not found response seq id:" +
                        std::to_string(sequenceID));
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


        class CompareWaitResponseTimer
        {
        public:
            bool operator() (const WaitResponseTimer& left, const WaitResponseTimer& right) const
            {
                return left.getEndTime() > right.getEndTime();
            }
        };

        std::priority_queue<WaitResponseTimer, std::vector<WaitResponseTimer>, CompareWaitResponseTimer>  mWaitResponseTimerQueue;
    };

} }
