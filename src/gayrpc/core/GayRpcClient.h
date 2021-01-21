#pragma once

#include <gayrpc/core/GayRpcHelper.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gayrpc::core {

class WaitResponseTimer final
{
public:
    WaitResponseTimer(uint64_t seqID, std::chrono::nanoseconds lastTime)
        : mEndTime(std::chrono::steady_clock::now() + lastTime),
          mSeqID(seqID)
    {
    }

    [[nodiscard]] const std::chrono::steady_clock::time_point& getEndTime() const
    {
        return mEndTime;
    }

    [[nodiscard]] uint64_t getSeqID() const
    {
        return mSeqID;
    }

private:
    std::chrono::steady_clock::time_point mEndTime;
    uint64_t mSeqID;
};

class BaseClient : public std::enable_shared_from_this<BaseClient>
{
public:
    using Ptr = std::shared_ptr<BaseClient>;
    using TimeoutCallback = std::function<void(void)>;
    using NetworkThreadChecker = std::function<bool(void)>;

public:
    const RpcTypeHandleManager::Ptr& getTypeHandleManager() const
    {
        return mTypeHandleManager;
    }

    const UnaryServerInterceptor& getInInterceptor() const
    {
        return mInboundInterceptor;
    }

    const UnaryServerInterceptor& getOutInterceptor() const
    {
        return mOutboundInterceptor;
    }

    bool inInNetworkThread() const
    {
        std::shared_lock<std::shared_mutex> lock(mNetworkThreadCheckerMutex);
        if (mNetworkThreadChecker == nullptr)
        {
            throw std::runtime_error("not setting network thread checker");
        }
        return mNetworkThreadChecker();
    }

    void setNetworkThreadChecker(NetworkThreadChecker checker)
    {
        std::unique_lock<std::shared_mutex> lock(mNetworkThreadCheckerMutex);
        mNetworkThreadChecker = std::move(checker);
    }

    void checkTimeout()
    {
        std::vector<TimeoutCallback> callbacks;
        {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lck(mStubMapGuard);
            while (!mWaitResponseTimerQueue.empty())
            {
                const auto& head = mWaitResponseTimerQueue.top();
                if (head.getEndTime() > now)
                {
                    break;
                }
                if (const auto it = mTimeoutHandleMap.find(head.getSeqID()); it != mTimeoutHandleMap.end())
                {
                    callbacks.push_back(std::move((*it).second));
                    mTimeoutHandleMap.erase(it);
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
    BaseClient(RpcTypeHandleManager::Ptr rpcHandlerManager,
               UnaryServerInterceptor inboundInterceptor,
               UnaryServerInterceptor outboundInterceptor)
        : mTypeHandleManager(std::move(rpcHandlerManager)),
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
              TimeoutCallback&& timeoutCallback)
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
            auto callback = [handle](RpcMeta&& meta,
                                     const std::string_view& data,
                                     const UnaryServerInterceptor& inboundInterceptor,
                                     InterceptorContextType&& context) {
                return parseResponseWrapper<Response>(handle,
                                                      std::move(meta),
                                                      data,
                                                      inboundInterceptor,
                                                      std::move(context));
            };

            std::lock_guard<std::mutex> lck(mStubMapGuard);
            assert(mStubHandleMap.find(sequenceID) == mStubHandleMap.end());
            assert(mTimeoutHandleMap.find(sequenceID) == mTimeoutHandleMap.end());

            mStubHandleMap[sequenceID] = std::move(callback);
            mTimeoutHandleMap[sequenceID] = std::move(timeoutCallback);
            mWaitResponseTimerQueue.push(waitTimer);
        }

        mOutboundInterceptor(
                std::move(meta),
                request,
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                    return ananas::MakeReadyFuture(std::optional<std::string>(std::nullopt));
                },
                InterceptorContextType{});
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
            std::lock_guard<std::mutex> lck(mStubMapGuard);
            assert(mStubHandleMap.find(sequenceID) == mStubHandleMap.end());

            mStubHandleMap[sequenceID] = [handle](RpcMeta&& meta,
                                                  const std::string_view& data,
                                                  const UnaryServerInterceptor& inboundInterceptor,
                                                  InterceptorContextType&& context) {
                return parseResponseWrapper<Response>(handle,
                                                      std::move(meta),
                                                      data,
                                                      inboundInterceptor,
                                                      std::move(context));
            };
        }

        mOutboundInterceptor(
                std::move(meta),
                request,
                [](RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&& context) {
                    return ananas::MakeReadyFuture(std::optional<std::string>(std::nullopt));
                },
                InterceptorContextType{});
    }

    void installResponseStub(const gayrpc::core::RpcTypeHandleManager::Ptr& rpcTypeHandleManager,
                             ServiceIDType serviceID)
    {
        auto responseStub = [sharedThis = shared_from_this(), this](RpcMeta&& meta,
                                                                    const std::string_view& data,
                                                                    InterceptorContextType&& context) {
            processRpcResponse(std::move(meta), data, std::move(context));
            return true;
        };
        rpcTypeHandleManager->registerTypeHandle(RpcMeta::RESPONSE, responseStub, serviceID);
    }

private:
    void processRpcResponse(RpcMeta&& meta, const std::string_view& data, InterceptorContextType&& context)
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
            TimeoutCallback timeoutHandler;
            {
                std::lock_guard<std::mutex> lck(mStubMapGuard);

                mStubHandleMap.erase(sequenceID);

                const auto it = mTimeoutHandleMap.find(sequenceID);
                if (it == mTimeoutHandleMap.end())
                {
                    return;
                }
                timeoutHandler = std::move((*it).second);
                mTimeoutHandleMap.erase(it);
            }
            if (timeoutHandler)
            {
                timeoutHandler();
            }

            return;
        }

        ResponseStubHandle handle;
        {
            std::lock_guard<std::mutex> lck(mStubMapGuard);

            mTimeoutHandleMap.erase(sequenceID);

            const auto it = mStubHandleMap.find(sequenceID);
            if (it == mStubHandleMap.end())
            {
                throw std::runtime_error("not found response seq id:" +
                                         std::to_string(sequenceID));
            }
            handle = std::move(it->second);
            mStubHandleMap.erase(it);
        }
        handle(std::move(meta), data, mInboundInterceptor, std::move(context));
    }

private:
    using ResponseStubHandle =
            std::function<
                    InterceptorReturnType(
                            RpcMeta&&,
                            const std::string_view& data,
                            const UnaryServerInterceptor&,
                            InterceptorContextType&& context)>;
    using ResponseStubHandleMap = std::unordered_map<uint64_t, ResponseStubHandle>;
    using TimeoutHandleMap = std::unordered_map<uint64_t, TimeoutCallback>;

    const RpcTypeHandleManager::Ptr mTypeHandleManager;
    const UnaryServerInterceptor mInboundInterceptor;
    const UnaryServerInterceptor mOutboundInterceptor;

    std::atomic<uint64_t> mSequenceID;
    std::mutex mStubMapGuard;
    ResponseStubHandleMap mStubHandleMap;
    TimeoutHandleMap mTimeoutHandleMap;

    class CompareWaitResponseTimer
    {
    public:
        bool operator()(const WaitResponseTimer& left, const WaitResponseTimer& right) const
        {
            return left.getEndTime() > right.getEndTime();
        }
    };

    std::priority_queue<WaitResponseTimer,
                        std::vector<WaitResponseTimer>,
                        CompareWaitResponseTimer>
            mWaitResponseTimerQueue;

    mutable std::shared_mutex mNetworkThreadCheckerMutex;
    NetworkThreadChecker mNetworkThreadChecker;
};

}// namespace gayrpc::core
