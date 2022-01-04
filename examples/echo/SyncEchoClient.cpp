#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/utils/UtilsInterceptor.h>

#include <brynet/net/wrapper/ConnectionBuilder.hpp>
#include <iostream>

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::test;

static EchoServerClient::Ptr createEchoClient(const TcpConnection::Ptr& session)
{
    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataCallback([rpcHandlerManager](brynet::base::BasePacketReader& reader) {
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, reader);
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

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: <host> <port>\n"
                  << std::endl;
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(1);

    auto connector = AsyncConnector::Create();
    connector->startWorkerThread();

    auto session = brynet::net::wrapper::ConnectionBuilder()
                           .WithService(service)
                           .WithConnector(connector)
                           .WithAddr(argv[1], atoi(argv[2]))
                           .WithTimeout(std::chrono::seconds(10))
                           .WithMaxRecvBufferSize(1024 * 1024)
                           .syncConnect();
    auto client = createEchoClient(session);

    {
        EchoRequest request;
        request.set_message("sync echo test");

        // TODO::同步RPC可以简单的使用 future 实现timeout
        // Warining::同步RPC不能在RPC网络线程中调用(会导致无法发出请求或者Response)
        auto responseFuture = client->SyncEcho(request, std::chrono::seconds(10));
        responseFuture.wait();
        auto result = responseFuture.result();
        const auto& response = result->first;
        const auto& error = result->second;
        (void) error;

        std::cout << "echo message:" << response.message() << std::endl;
    }

    {
        LoginRequest request;
        request.set_message("sync login test");

        auto responseFuture = client->SyncLogin(request, std::chrono::seconds(10));
        responseFuture.wait();
        auto result = responseFuture.result();
        const auto& response = result->first;
        const auto& error = result->second;
        (void) error;

        std::cout << "login message:" << response.message() << std::endl;
    }

    return 0;
}
