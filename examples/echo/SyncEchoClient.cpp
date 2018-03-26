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

static echo_service::EchoServerClient::PTR createEchoClient(const TCPSession::PTR& session)
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
    return client;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: <host> <port>\n");
        exit(-1);
    }

    auto service = std::make_shared<WrapTcpService>();
    service->startWorkThread(std::thread::hardware_concurrency());

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();

    auto clientPromise = std::make_shared<std::promise<echo_service::EchoServerClient::PTR>>();

    connector->asyncConnect(
        argv[1],
        atoi(argv[2]),
        std::chrono::seconds(10),
        [service, clientPromise](TcpSocket::PTR socket) {
        std::cout << "connect success" << std::endl;
        socket->SocketNodelay();
        service->addSession(
            std::move(socket),
            [clientPromise](const TCPSession::PTR& session) {
                auto client = createEchoClient(session);
                clientPromise->set_value(client);
            },
            false,
            nullptr,
            1024 * 1024);
    }, []() {
        std::cout << "connect failed" << std::endl;
    });

    auto client = clientPromise->get_future().get();

    {
        gayrpc::core::RpcError error;
        EchoRequest request;
        request.set_message("sync echo test");

        // TODO::timeout
        // Warining::同步RPC不能在RPC网络线程中调用(会导致无法发出请求或者Response)
        auto response = client->sync_echo(request, error);

        std::cout << "echo result:" << error.failed() << std::endl;
        std::cout << "echo message:" << response.message() << std::endl;
    }

    {
        gayrpc::core::RpcError error;
        LoginRequest request;
        request.set_message("sync login test");

        auto response = client->sync_login(request, error);

        std::cout << "login result:" << error.failed() << std::endl;
        std::cout << "login message:" << response.message() << std::endl;
    }

    return 0;
}
