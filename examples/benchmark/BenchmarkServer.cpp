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
        const EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response);
    }
};

static void counter(const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next)
{
    count++;
    next(meta, message);
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

    auto binaryListenThread = ListenThread::Create();
    gayrpc::utils::StartBinaryRpcServer<EchoServerService>(service, binaryListenThread, "0.0.0.0", std::stoi(argv[1]), [](gayrpc::core::ServiceContext context) {
        return std::make_shared<MyService>(context);
    }, counter, counter, nullptr, 1024 * 1024, std::chrono::seconds(10));

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);

    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
