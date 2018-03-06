#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

#include <brynet/net/SocketLibFunction.h>
#include <brynet/net/WrapTCPService.h>
#include <brynet/net/Connector.h>
#include <brynet/utils/packet.h>

#include "OpPacket.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"
#include "UtilsInterceptor.h"
#include "WaitGroup.h"

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace dodo::benchmark;

typedef std::vector<std::chrono::nanoseconds> LATENTY_TYPE;
typedef std::shared_ptr<LATENTY_TYPE> LATENCY_PTR;

class BenchmarkClient : public std::enable_shared_from_this<BenchmarkClient>
{
public:
    BenchmarkClient(const benchmark_service::EchoServerClient::PTR& client,
        const WaitGroup::PTR& wg,
        int maxNum,
        LATENCY_PTR latency,
        std::string payload)
    {
        mClient = client;
        mWg = wg;
        maxRequestNum = maxNum;
        mCurrentNum = 0;
        mLatency = latency;
        mPayload = payload;
    }

    void sendRequest()
    {
        // 发送RPC请求
        EchoRequest request;
        request.set_message(mPayload);

        mRequestTime = std::chrono::steady_clock::now();
        mClient->echo(request, std::bind(&BenchmarkClient::onEchoResponse, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    }

private:
    void    onEchoResponse(const EchoResponse& response,
        const gayrpc::core::RpcError& error)
    {
        mCurrentNum++;
        mLatency->push_back((std::chrono::steady_clock::now() - mRequestTime));

        if (error.failed())
        {
            std::cout << "reason" << error.reason() << std::endl;
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
    WaitGroup::PTR                              mWg;
    benchmark_service::EchoServerClient::PTR    mClient;
    int                                         mCurrentNum;
    int                                         maxRequestNum;
    LATENCY_PTR                                 mLatency;
    std::chrono::steady_clock::time_point       mRequestTime;
    std::string                                 mPayload;
};

std::atomic<int64_t> connectionCounter(0);

static void onConnection(const TCPSession::PTR& session,
    const WaitGroup::PTR& wg,
    int maxRequestNum,
    LATENCY_PTR latency,
    std::string payload)
{
    connectionCounter++;
    std::cout << "connection counter is:" << connectionCounter << std::endl;

    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataCallback([rpcHandlerManager](const TCPSession::PTR& session,
        const char* buffer,
        size_t len) {
        return dataHandle(rpcHandlerManager, buffer, len);
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall());

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::utils::makeInterceptor(withSessionSender(std::weak_ptr<TCPSession>(session)));

    // 注册RPC客户端
    auto client = benchmark_service::EchoServerClient::Create(rpcHandlerManager, outBoundInterceptor, inboundInterceptor);
    auto b = std::make_shared<BenchmarkClient>(client, wg, maxRequestNum, latency, payload);
    b->sendRequest();
}

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: <host> <port> <client num> <total request num> <payload size>\n");
        exit(-1);
    }

    auto server = std::make_shared<WrapTcpService>();
    server->startWorkThread(std::thread::hardware_concurrency());

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();
    auto clientNum = std::atoi(argv[3]);
    auto maxRequestNumEveryClient = std::atoi(argv[4]) / clientNum;
    auto realyTotalRequestNum = maxRequestNumEveryClient * clientNum;
    auto payload = std::string(std::atoi(argv[5]), 'a');

    auto wg = WaitGroup::Create();

    std::vector<LATENCY_PTR> latencyArray;

    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < clientNum; i++)
    {
        wg->add();

        try
        {
            auto latency = std::make_shared<LATENTY_TYPE>();
            latencyArray.push_back(latency);

            connector->asyncConnect(
                argv[1],
                atoi(argv[2]),
                std::chrono::seconds(10),
                [server, wg, maxRequestNumEveryClient, latency, payload](TcpSocket::PTR socket) {
                std::cout << "connect success" << std::endl;
                socket->SocketNodelay();
                server->addSession(
                    std::move(socket),
                    std::bind(onConnection, std::placeholders::_1, wg, maxRequestNumEveryClient, latency, payload),
                    false,
                    nullptr,
                    1024 * 1024);
            }, []() {
                std::cout << "connect failed" << std::endl;
            });
        }
        catch (std::runtime_error& e)
        {
            std::cout << "error:" << e.what() << std::endl;
        }
    }
    
    wg->wait();

    auto nowTime = std::chrono::steady_clock::now();

    std::chrono::nanoseconds totalLatenty = std::chrono::nanoseconds::zero();
    LATENTY_TYPE tmp1;

    for (auto& v : latencyArray)
    {
        for (auto& latency: *v)
        {
            totalLatenty += latency;
            tmp1.push_back(latency);
        }
    }
    std::sort(tmp1.begin(), tmp1.end());

    auto costTime = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime);

    std::cout << "connection num:"
        << connectionCounter
        << std::endl;

    std::cout << "took " 
        << costTime.count() 
        << "ms, for " 
        << realyTotalRequestNum 
        << " requests" 
        << std::endl;

    std::cout << "throughput  (TPS):"
        << (realyTotalRequestNum/(std::chrono::duration_cast<std::chrono::seconds>(costTime)).count())
        << std::endl;

    std::cout << "mean:"
        << (std::chrono::duration_cast<std::chrono::milliseconds>(totalLatenty).count() / realyTotalRequestNum)
        << " ms ,"
        << (totalLatenty.count() / realyTotalRequestNum)
        << " ns" 
        << std::endl;

    if (tmp1.empty())
    {
        std::cout << "latenty is empty" << std::endl;
        return 0;
    }

    std::cout << "median:"
        << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[tmp1.size()/2]).count())
        << " ms ,"
        << (tmp1[tmp1.size() / 2].count())
        << " ns"
        << std::endl;

    std::cout << "max:"
        << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[tmp1.size()-1]).count())
        << " ms ,"
        << (tmp1[tmp1.size() - 1].count())
        << " ns"
        << std::endl;

    std::cout << "min:"
        << (std::chrono::duration_cast<std::chrono::milliseconds>(tmp1[0]).count())
        << " ms ,"
        << (tmp1[0].count())
        << " ns"
        << std::endl;

    auto p99Index = tmp1.size() * 99 / 100;
    std::chrono::nanoseconds p99Total = std::chrono::nanoseconds::zero();
    for (size_t i = 0; i < p99Index; i++)
    {
        p99Total += tmp1[i];
    }
    std::cout << "p99:"
        << (std::chrono::duration_cast<std::chrono::milliseconds>(p99Total).count()/ p99Index)
        << " ms ,"
        << (p99Total.count() / p99Index)
        << " ns"
        << std::endl;
    return 0;
}
