#include <iostream>
#include <string>

#include <brynet/net/TcpService.hpp>
#include <brynet/net/AsyncConnector.hpp>
#include <brynet/base/AppStatus.hpp>

#include <gayrpc/utils/UtilsWrapper.h>

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace gayrpc::utils;
using namespace dodo::test;

static brynet::net::EventLoop::Ptr mainLoop;

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        :
        EchoServerService(std::move(context))
    {
    }

    void Echo(const EchoRequest& request,
        const EchoReply::PTR& replyObj,
        InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response, std::move(context));
    }

    void Login(const LoginRequest& request,
        const LoginReply::PTR& replyObj,
        InterceptorContextType&& context) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response, std::move(context));
    }

    void    onClose() override
    {
        std::cout << "closed" << std::endl;
    }
};

static void sendEchoRequest(dodo::test::EchoServerClient::PTR client)
{
    // 发送RPC请求
    EchoRequest request;
    request.set_message("hello");
    client->Echo(request, [client](const EchoResponse & response,
        const gayrpc::core::RpcError & error) {
            if (error.failed())
            {
                std::cout << "reason" << error.reason() << std::endl;
                return;
            }
            sendEchoRequest(client);
        });
}

static void OnConnection(dodo::test::EchoServerClient::PTR client, size_t batchNum)
{
    gayrpc::core::ServiceContext context(client->getTypeHandleManager(), client->getInInterceptor(), client->getOutInterceptor());
    auto service = std::make_shared< MyService>(std::move(context));
    dodo::test::EchoServerService::Install(service);

    for (size_t i = 0; i < batchNum; i++)
    {
        sendEchoRequest(client);
    }
}

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: <host> <port> <client num> <thread num> <batch num>\n");
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(static_cast<size_t>(std::atoi(argv[4])));

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();
    auto clientNum = std::atoi(argv[3]);
    size_t batchNum = static_cast<size_t>(std::atoi(argv[5]));

    mainLoop = std::make_shared<brynet::net::EventLoop>();
    
    auto b = ClientBuilder();
    b.buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
                //buildInterceptors.addInterceptor(gayrpc::utils::withEventLoop(mainLoop));
            })
        .buildOutboundInterceptor([](BuildInterceptor buildInterceptors) {
            })
        .configureConnectionOptions({
            brynet::net::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            brynet::net::AddSocketOption::AddEnterCallback([&](const TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            })
        })
        .configureConnector(connector)
        .configureService(service);

    for (int i = 0; i < clientNum; i++)
    {
        try
        {
            b.configureConnectOptions({
                    ConnectOption::WithAddr(argv[1], std::stoi(argv[2])),
                    ConnectOption::WithTimeout(std::chrono::seconds(10)),
                })
                .asyncConnect<EchoServerClient>([=](dodo::test::EchoServerClient::PTR client) {
                    OnConnection(client, batchNum);
                });

        }
        catch (std::runtime_error& e)
        {
            std::cout << "error:" << e.what() << std::endl;
        }
    }

    while (true)
    {
        mainLoop->loop(1000);
        if(brynet::base::app_kbhit() > 0)
        {
            break;
        }
    }

    return 0;
}
