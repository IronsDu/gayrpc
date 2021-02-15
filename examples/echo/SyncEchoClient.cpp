#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/utils/UtilsInterceptor.h>

#include <bsio/Bsio.hpp>
#include <bsio/net/wrapper/ConnectorBuilder.hpp>
#include <iostream>
#include <utility>

#include "./pb/echo_service.gayrpc.h"

using namespace bsio::net;
using namespace gayrpc::utils;
using namespace dodo::test;

static EchoServerClient::Ptr createEchoClient(const bsio::net::TcpSession::Ptr& session)
{
    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataHandler([rpcHandlerManager](const bsio::net::TcpSession::Ptr& session, bsio::base::BasePacketReader& reader) {
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, reader);
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::core::makeInterceptor(gayrpc::utils::withProtectedCall());

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::core::makeInterceptor(gayrpc::utils::withSessionBinarySender(std::weak_ptr<bsio::net::TcpSession>(session)));

    // 注册RPC客户端
    auto client = EchoServerClient::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
    return client;
}

static bsio::net::TcpSession::Ptr syncConnect(TcpConnector connector, asio::ip::tcp::endpoint endpoint)
{
    auto sessionPromise = std::make_shared<std::promise<bsio::net::TcpSession::Ptr>>();
    bsio::net::wrapper::TcpSessionConnectorBuilder builder;
    builder.WithConnector(std::move(connector))
            .WithEndpoint(std::move(endpoint))
            .WithRecvBufferSize(1024)
            .WithFailedHandler([sessionPromise]() {
                sessionPromise->set_value(nullptr);
            })
            .WithTimeout(std::chrono::seconds(10))
            .AddEnterCallback([sessionPromise](bsio::net::TcpSession::Ptr session) {
                sessionPromise->set_value(session);
            })
            .asyncConnect();
    auto future = sessionPromise->get_future();
    if (future.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
    {
        return nullptr;
    }
    return future.get();
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: <host> <port>" << std::endl;
        exit(-1);
    }

    IoContextThreadPool::Ptr ioContextPool = IoContextThreadPool::Make(1, 1);
    ioContextPool->start(1);
    auto session = syncConnect(TcpConnector(ioContextPool),
                               asio::ip::tcp::endpoint(
                                       asio::ip::address_v4::from_string(argv[1]), std::atoi(argv[2])));

    auto client = createEchoClient(session);

    {
        EchoRequest request;
        request.set_message("sync echo test");

        // TODO::同步RPC可以简单的使用 future 实现timeout
        // Warning::同步RPC不能在RPC网络线程中调用(会导致无法发出请求或者Response)
        auto responseFuture = client->SyncEcho(request, std::chrono::seconds(10));
        auto result = responseFuture.Wait();
        const auto& response = result.Value().first;
        const auto& error = result.Value().second;
        (void) error;

        std::cout << "echo message:" << response.message() << std::endl;
    }

    {
        LoginRequest request;
        request.set_message("sync login test");

        auto responseFuture = client->SyncLogin(request, std::chrono::seconds(10));
        auto result = responseFuture.Wait();
        const auto& response = result.Value().first;
        const auto& error = result.Value().second;
        (void) error;

        std::cout << "login message:" << response.message() << std::endl;
    }

    return 0;
}
