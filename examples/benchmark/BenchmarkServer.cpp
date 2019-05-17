#include <iostream>
#include <atomic>

#include <brynet/net/EventLoop.h>
#include <brynet/net/TCPService.h>
#include <brynet/net/ListenThread.h>

#include <gayrpc/utils/UtilsWrapper.h>

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::benchmark;
using namespace gayrpc::core;
using namespace gayrpc::utils;

std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    MyService(gayrpc::core::ServiceContext context)
        :
        EchoServerService(context)
    {}

    void Echo(const EchoRequest& request, 
        const EchoReply::PTR& replyObj,
        InterceptorContextType context) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response, std::move(context));
    }
};

static void counter(const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next, InterceptorContextType context)
{
    count++;
    next(meta, message, std::move(context));
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: <listen port>\n");
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(std::thread::hardware_concurrency());

    auto config = gayrpc::utils::WrapTcpRpc<EchoServerService>(service,
        [](gayrpc::core::ServiceContext context) {
            return std::make_shared<MyService>(context);
        }, 
        {
            TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            TcpService::AddSocketOption::AddEnterCallback([](const TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            }),
        },
        {
            RpcConfig::WithOutboundInterceptor(counter),
        });
    auto binaryListenThread = brynet::net::ListenThread::Create(false, "0.0.0.0", std::stoi(argv[1]), config);
    binaryListenThread->startListen();

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);

    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
