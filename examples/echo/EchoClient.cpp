#include <iostream>
#include <string>

#include <brynet/net/SocketLibFunction.h>
#include <brynet/net/WrapTCPService.h>
#include <brynet/net/Connector.h>
#include <brynet/utils/packet.h>

#include "OpPacket.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"
#include "UtilsInterceptor.h"
#include "GayRpcClient.h"

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace dodo::test;

class MyService : public echo_service::EchoServerService
{
public:
    MyService(const std::shared_ptr<echo_service::EchoServerClient>& client)
        :
        mClient(client)
    {
    }

    bool echo(const EchoRequest& request,
        const echo_service::EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response); // 重复reply或error将产生异常
        // 在收到请求后再调用对端
        mClient->echo(request, [](const EchoResponse& response, const gayrpc::core::RpcError& err) {
            err.failed();
        });

        return true;
    }

    bool login(const LoginRequest& request,
        const echo_service::LoginReply::PTR& replyObj) override
    {
        return true;
    }

private:
    std::shared_ptr<echo_service::EchoServerClient>   mClient;
};

static void onConnection(const TCPSession::PTR& session)
{
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
    auto client = echo_service::EchoServerClient::Create(rpcHandlerManager, outBoundInterceptor, inboundInterceptor);

    auto rpcServer = std::make_shared<MyService>(client);
    registerEchoServerService(rpcHandlerManager, rpcServer, inboundInterceptor, outBoundInterceptor);

    session->setDisConnectCallback([rpcServer](const TCPSession::PTR& session) {
        std::cout << "close session" << std::endl;
        rpcServer->onClose();
    });

    // 发送RPC请求
    EchoRequest request;
    request.set_message("hello");

    client->echo(request, [](const EchoResponse& response,
        const gayrpc::core::RpcError& error) {
        if (error.failed())
        {
            std::cout << "reason" << error.reason() << std::endl;
            return;
        }
        //std::cout << "recv reply, data:" << response.message() << std::endl;
    });
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: <host> <port> <num>\n");
        exit(-1);
    }

    auto server = std::make_shared<WrapTcpService>();
    server->startWorkThread(std::thread::hardware_concurrency());

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();
    auto num = std::atoi(argv[3]);

    for (int i = 0; i < num; i++)
    {
        try
        {
            connector->asyncConnect(
                argv[1],
                atoi(argv[2]),
                std::chrono::seconds(10),
                [server](TcpSocket::PTR socket) {
                std::cout << "connect success" << std::endl;
                socket->SocketNodelay();
                server->addSession(
                    std::move(socket),
                    onConnection,
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
    

    std::cin.get();
}
