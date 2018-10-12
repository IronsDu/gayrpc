#include <iostream>
#include <string>

#include <brynet/net/TCPService.h>
#include <brynet/net/Connector.h>

#include "UtilsWrapper.h"

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace dodo::test;

class MyService : public EchoServerService
{
public:
    MyService(gayrpc::core::ServiceContext context)
        :
        EchoServerService(context)
    {
    }

    void Echo(const EchoRequest& request,
        const EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response);
    }

    void Login(const LoginRequest& request,
        const LoginReply::PTR& replyObj) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response);
    }

private:
};

static void OnConnection(dodo::test::EchoServerClient::PTR client)
{
    gayrpc::core::ServiceContext context(client->getTypeHandleManager(), client->getInInterceptor(), client->getOutInterceptor());
    auto service = std::make_shared< MyService>(context);
    dodo::test::EchoServerService::Install(service);

    // 发送RPC请求
    EchoRequest request;
    request.set_message("hello");

    client->Echo(request, [](const EchoResponse& response,
        const gayrpc::core::RpcError& error) {
        if (error.failed())
        {
            std::cout << "reason" << error.reason() << std::endl;
            return;
        }
        //std::cout << "recv reply, data:" << response.message() << std::endl;
    }, std::chrono::seconds(3),
        []() {
        std::cout << "timeout" << std::endl;
    });
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: <host> <port> <num>\n");
        exit(-1);
    }

    auto server = TcpService::Create();
    server->startWorkerThread(std::thread::hardware_concurrency());

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();
    auto num = std::atoi(argv[3]);

    auto mainLoop = std::make_shared<brynet::net::EventLoop>();

    for (int i = 0; i < num; i++)
    {
        try
        {
            utils_wrapper::AsyncCreateRpcClient< EchoServerClient>(server, connector,
                argv[1], std::stoi(argv[2]), std::chrono::seconds(10),
                nullptr, nullptr,
                [=]() -> brynet::net::EventLoop::PTR {
                    return mainLoop;
                }, [](dodo::test::EchoServerClient::PTR client) {
                    OnConnection(client);
                }, []() {}, 1024 * 1024);
        }
        catch (std::runtime_error& e)
        {
            std::cout << "error:" << e.what() << std::endl;
        }
    }

    while (true)
    {
        mainLoop->loop(1);
    }

    return 0;
}
