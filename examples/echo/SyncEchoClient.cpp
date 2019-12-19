#include <iostream>

#include <brynet/net/wrapper/ConnectionBuilder.hpp>

#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/utils/UtilsInterceptor.h>

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::test;

static EchoServerClient::PTR createEchoClient(const TcpConnection::Ptr& session)
{
    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataCallback([rpcHandlerManager](const char* buffer,
        size_t len) {
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, buffer, len);
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::core::makeInterceptor(gayrpc::utils::withProtectedCall());

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::core::makeInterceptor(gayrpc::utils::withSessionBinarySender(std::weak_ptr<TcpConnection>(session)),
        gayrpc::utils::withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));

    // 注册RPC客户端
    auto client = EchoServerClient::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
    return client;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: <host> <port>\n" << std::endl;
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(1);

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();

    auto session = brynet::net::wrapper::ConnectionBuilder()
        .configureService(service)
        .configureConnector(connector)
        .configureConnectOptions( {
                ConnectOption::WithAddr(argv[1], atoi(argv[2])),
                ConnectOption::WithTimeout(std::chrono::seconds(10)),
        })
        .configureConnectionOptions({
            brynet::net::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024)
        })
        .syncConnect();
    auto client = createEchoClient(session);

    {
        EchoRequest request;
        request.set_message("sync echo test");

        // TODO::同步RPC可以简单的使用 future 实现timeout
        // Warining::同步RPC不能在RPC网络线程中调用(会导致无法发出请求或者Response)
        auto responseFuture = client->SyncEcho(request, std::chrono::seconds(10));
        auto result = responseFuture.Wait();
        const auto& response = result.Value().first;
        const auto& error = result.Value().second;

        std::cout << "echo message:" << response.message() << std::endl;
    }

    {
        LoginRequest request;
        request.set_message("sync login test");

        auto responseFuture = client->SyncLogin(request, std::chrono::seconds(10));
        auto result = responseFuture.Wait();
        const auto& response = result.Value().first;
        const auto& error = result.Value().second;

        std::cout << "login message:" << response.message() << std::endl;
    }

    return 0;
}
