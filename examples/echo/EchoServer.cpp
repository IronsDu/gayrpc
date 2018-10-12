#include <iostream>
#include <atomic>

#include <brynet/net/EventLoop.h>
#include <brynet/net/TCPService.h>
#include <brynet/net/ListenThread.h>

#include "UtilsWrapper.h"
#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace gayrpc::core;
using namespace dodo::test;

std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    MyService(gayrpc::core::ServiceContext context)
        :
        EchoServerService(context)
    {
        mClient = EchoServerClient::Create(context.getTypeHandleManager(), context.getInInterceptor(), context.getOutInterceptor());
    }

    void Echo(const EchoRequest& request, 
        const EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response);

        if (mClient != nullptr)
        {
            // 演示双向RPC(调用对端服务)
            EchoRequest r;
            r.set_message("hello");
            mClient->Echo(r, [](const EchoResponse& response, const gayrpc::core::RpcError& err) {
                err.failed();
            });
        }
    }

    void Login(const LoginRequest& request,
        const LoginReply::PTR& replyObj) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response);
    }

private:
    std::shared_ptr<EchoServerClient>   mClient;
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
    utils_wrapper::StartBinaryRpcServer<EchoServerService>(service, binaryListenThread, "0.0.0.0", std::stoi(argv[1]), [](gayrpc::core::ServiceContext context) {
        return std::make_shared<MyService>(context);
    }, counter, counter, nullptr, 1024 * 1024);

    auto httpListenThread = ListenThread::Create();
    utils_wrapper::StartHttpRpcServer<EchoServerService>(service, httpListenThread, "0.0.0.0", 80, [](gayrpc::core::ServiceContext context) {
        return std::make_shared<MyService>(context);
    }, counter, counter, nullptr, 1024 * 1024);

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);
    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
