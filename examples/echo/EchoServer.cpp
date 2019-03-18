#include <iostream>
#include <atomic>

#include <brynet/net/EventLoop.h>
#include <brynet/net/TCPService.h>
#include <brynet/net/ListenThread.h>

#include <gayrpc/utils/UtilsWrapper.h>
#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace gayrpc::core;
using namespace dodo::test;
using namespace gayrpc::utils;

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
        const EchoReply::PTR& replyObj,
        InterceptorContextType context) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response, std::move(context));

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
        const LoginReply::PTR& replyObj,
        InterceptorContextType context) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response, std::move(context));
    }

private:
    std::shared_ptr<EchoServerClient>   mClient;
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

    auto binaryServiceConfig = gayrpc::utils::WrapTcpRpc<EchoServerService>(
        service,
        [](gayrpc::core::ServiceContext context) {
            return std::make_shared<MyService>(context);
        },
        {
            TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            TcpService::AddSocketOption::AddEnterCallback([](const TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            })
        },
        {
            RpcConfig::WithInboundInterceptor(counter),
            RpcConfig::WithOutboundInterceptor(counter),
        });
    auto binaryListenThread = ListenThread::Create(false, "0.0.0.0", std::stoi(argv[1]), binaryServiceConfig);
    binaryListenThread->startListen();

    auto httpServiceConfig = gayrpc::utils::WrapHttpRpc<EchoServerService>(service,
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
            RpcConfig::WithInboundInterceptor(counter),
            RpcConfig::WithOutboundInterceptor(counter)
        });
    auto httpListenThread = ListenThread::Create(false, "0.0.0.0", 80, httpServiceConfig);
    httpListenThread->startListen();

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);
    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
