#include <gayrpc/utils/UtilsWrapper.h>

#include <bsio/Bsio.hpp>
#include <bsio/net/wrapper/ConnectorBuilder.hpp>
#include <iostream>
#include <string>

#include "./pb/echo_service.gayrpc.h"

using namespace bsio::net;
using namespace gayrpc::utils;
using namespace dodo::test;

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        : EchoServerService(std::move(context))
    {
    }

    void Echo(const EchoRequest& request,
              const EchoReply::Ptr& replyObj,
              InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response, std::move(context));
    }

    void Login(const LoginRequest& request,
               const LoginReply::Ptr& replyObj,
               InterceptorContextType&& context) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response, std::move(context));
    }

    void onClose() override
    {
        std::cout << "closed" << std::endl;
    }
};

static void sendEchoRequest(const dodo::test::EchoServerClient::Ptr& client)
{
    // 发送RPC请求
    EchoRequest request;
    request.set_message("hello");
    client->Echo(request, [client](const EchoResponse& response,
                                   std::optional<gayrpc::core::RpcError> error) {
        if (error)
        {
            std::cout << "reason:" << error->reason() << std::endl;
            return;
        }
        sendEchoRequest(client);
    });
}

static void OnConnection(const dodo::test::EchoServerClient::Ptr& client, size_t batchNum)
{
    gayrpc::core::ServiceContext context(client->getTypeHandleManager(), client->getInInterceptor(), client->getOutInterceptor());
    auto service = std::make_shared<MyService>(std::move(context));
    dodo::test::EchoServerService::Install(service);

    for (size_t i = 0; i < batchNum; i++)
    {
        sendEchoRequest(client);
    }
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: <host> <port> <client num> <thread num> <batch num>\n");
        exit(-1);
    }

    IoContextThreadPool::Ptr ioContextPool = IoContextThreadPool::Make(1, 1);
    ioContextPool->start(1);

    auto clientNum = std::atoi(argv[3]);
    auto batchNum = static_cast<size_t>(std::atoi(argv[5]));

    auto b = ClientBuilder();
    b.buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
         //buildInterceptors.addInterceptor(gayrpc::utils::withEventLoop(mainLoop));
     })
            .buildOutboundInterceptor([](BuildInterceptor buildInterceptors) {
            })
            .WithConnector(TcpConnector(ioContextPool))
            .WithRecvBufferSize(1024 * 1024);

    for (int i = 0; i < clientNum; i++)
    {
        try
        {
            b.WithEndpoint(asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(argv[1]), std::atoi(argv[2])))
                    .WithTimeout(std::chrono::seconds(10))
                    .WithFailedHandler([]() {
                        std::cout << "connect failed" << std::endl;
                    })
                    .asyncConnect<EchoServerClient>([=](const dodo::test::EchoServerClient::Ptr& client) {
                        OnConnection(client, batchNum);
                    });
        }
        catch (std::runtime_error& e)
        {
            std::cout << "error:" << e.what() << std::endl;
        }
    }

    WrapperIoContext mainLoop(1);

    asio::signal_set sig(mainLoop.context(), SIGINT, SIGTERM);
    sig.async_wait([&](const asio::error_code& err, int signal) {
        mainLoop.stop();
    });

    for (; !mainLoop.context().stopped();)
    {
        mainLoop.context().run_one_for(std::chrono::seconds(1));
    }

    return 0;
}
