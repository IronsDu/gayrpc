#include <gayrpc/utils/UtilsWrapper.h>

#include <algorithm>
#include <brynet/base/WaitGroup.hpp>
#include <brynet/net/AsyncConnector.hpp>
#include <brynet/net/TcpService.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::benchmark;
using namespace gayrpc::utils;

typedef std::vector<std::chrono::nanoseconds> LatencyType;
typedef std::shared_ptr<LatencyType> LatencyPtr;

class BenchmarkClient : public std::enable_shared_from_this<BenchmarkClient>
{
public:
    BenchmarkClient(EchoServerClient::Ptr client,
                    brynet::base::WaitGroup::Ptr wg,
                    int maxNum,
                    LatencyPtr latency,
                    std::string payload)
        : maxRequestNum(maxNum),
          mClient(std::move(client)),
          mWg(std::move(wg)),
          mPayload(std::move(payload)),
          mCurrentNum(0),
          mLatency(std::move(latency))
    {
    }

    void sendRequest()
    {
        // 发送RPC请求
        EchoRequest request;
        request.set_message(mPayload);

        mRequestTime = std::chrono::steady_clock::now();
        mClient->Echo(request,
                      [sharedThis = shared_from_this(), this](const EchoResponse& response,
                                                              const std::optional<gayrpc::core::RpcError>& error) {
                          onEchoResponse(response, error);
                      });
    }

private:
    void onEchoResponse(const EchoResponse& response,
                        const std::optional<gayrpc::core::RpcError>& error)
    {
        (void) response;
        mCurrentNum++;
        mLatency->push_back((std::chrono::steady_clock::now() - mRequestTime));

        if (error)
        {
            std::cout << "reason" << error.value().reason() << std::endl;
            return;
        }

        if (mCurrentNum < maxRequestNum)
        {
            sendRequest();
        }
        else
        {
            mWg->done();
        }
    }

private:
    const int maxRequestNum;
    const EchoServerClient::Ptr mClient;
    const brynet::base::WaitGroup::Ptr mWg;
    const std::string mPayload;

    int mCurrentNum;
    LatencyPtr mLatency;
    std::chrono::steady_clock::time_point mRequestTime;
};

static std::atomic<int64_t> connectionCounter(0);

static void onConnection(const dodo::benchmark::EchoServerClient::Ptr& client,
                         const brynet::base::WaitGroup::Ptr& wg,
                         int maxRequestNum,
                         const LatencyPtr& latency,
                         const std::string& payload)
{
    connectionCounter++;
    std::cout << "connection counter is:" << connectionCounter << std::endl;
    auto b = std::make_shared<BenchmarkClient>(client, wg, maxRequestNum, latency, payload);
    b->sendRequest();
}

static void outputLatency(int totalRequestNum,
                          const std::vector<LatencyPtr>& latencyArray,
                          std::chrono::steady_clock::time_point startTime)
{
    auto nowTime = std::chrono::steady_clock::now();

    std::chrono::nanoseconds totalLatency = std::chrono::nanoseconds::zero();
    LatencyType tmp1;

    for (const auto& v : latencyArray)
    {
        for (const auto& latency : *v)
        {
            totalLatency += latency;
            tmp1.push_back(latency);
        }
    }
    std::sort(tmp1.begin(), tmp1.end());

    auto costTime = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime);

    std::cout << "connection num:"
              << connectionCounter
              << std::endl;

    std::cout << "cost "
              << costTime.count()
              << " ms for "
              << totalRequestNum
              << " requests"
              << std::endl;

    auto second = std::chrono::duration_cast<std::chrono::seconds>(costTime).count();
    if (second == 0)
    {
        second = 1;
    }
    std::cout << "throughput(TPS):"
              << (totalRequestNum / second)
              << std::endl;

    std::cout << "mean:"
              << (std::chrono::duration_cast<std::chrono::milliseconds>(totalLatency).count() / totalRequestNum)
              << " ms, "
              << (totalLatency.count() / totalRequestNum)
              << " ns"
              << std::endl;

    if (tmp1.empty())
    {
        std::cout << "latency is empty" << std::endl;
        return;
    }

    std::cout << "median:"
              << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[tmp1.size() / 2]).count())
              << " ms, "
              << (tmp1[tmp1.size() / 2].count())
              << " ns"
              << std::endl;

    std::cout << "max:"
              << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[tmp1.size() - 1]).count())
              << " ms, "
              << (tmp1[tmp1.size() - 1].count())
              << " ns"
              << std::endl;

    std::cout << "min:"
              << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[0]).count())
              << " ms, "
              << (tmp1[0].count())
              << " ns"
              << std::endl;

    auto p99Index = tmp1.size() * 99 / 100;
    if (p99Index == 0)
    {
        p99Index = 1;
    }

    std::cout << "p99:"
              << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[p99Index]).count())
              << " ms, "
              << (tmp1[p99Index].count())
              << " ns"
              << std::endl;
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: <host> <port> <client num> <total request num> <payload size>\n");
        exit(-1);
    }

    auto server = TcpService::Create();
    server->startWorkerThread(std::thread::hardware_concurrency());

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();
    auto clientNum = std::stoi(argv[3]);
    auto maxRequestNumEveryClient = std::stoi(argv[4]) / clientNum;
    auto totalRequestNum = maxRequestNumEveryClient * clientNum;
    auto payload = std::string(std::stoi(argv[5]), 'a');

    auto wg = brynet::base::WaitGroup::Create();

    std::vector<LatencyPtr> latencyArray;

    auto startTime = std::chrono::steady_clock::now();

    auto b = ClientBuilder();
    b.WithConnector(connector)
            .WithService(server)
            .buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
                (void) buildInterceptors;
            })
            .buildOutboundInterceptor([](BuildInterceptor buildInterceptors) {
                (void) buildInterceptors;
            })
            .WithMaxRecvBufferSize(1024 * 1024)
            .WithAddr(argv[1], std::stoi(argv[2]))
            .WithTimeout(std::chrono::seconds(10))
            .WithFailedCallback([=]() {
                std::cout << "connect " << argv[1] << ":" << argv[2]
                          << " failed " << std::endl;
            });

    for (int i = 0; i < clientNum; i++)
    {
        wg->add();

        try
        {
            auto latency = std::make_shared<LatencyType>();
            latencyArray.push_back(latency);

            b.asyncConnect<EchoServerClient>([=](const EchoServerClient::Ptr& client) {
                onConnection(client, wg, maxRequestNumEveryClient, latency, payload);
            });
        }
        catch (std::runtime_error& e)
        {
            std::cout << "error:" << e.what() << std::endl;
        }
    }

    wg->wait(std::chrono::seconds(100));

    outputLatency(totalRequestNum, latencyArray, startTime);

    return 0;
}
