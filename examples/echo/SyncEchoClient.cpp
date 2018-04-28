#include <iostream>

#include <brynet/net/SyncConnector.h>

#include "UtilsDataHandler.h"
#include "UtilsInterceptor.h"

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace dodo::test;

static EchoServerClient::PTR createEchoClient(const TCPSession::PTR& session)
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
    auto outBoundInterceptor = gayrpc::utils::makeInterceptor(withSessionSender(std::weak_ptr<TCPSession>(session)),
        withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));

    // 注册RPC客户端
    auto client = EchoServerClient::Create(rpcHandlerManager, outBoundInterceptor, inboundInterceptor);
    return client;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: <host> <port>\n" << std::endl;
        exit(-1);
    }

    auto service = std::make_shared<WrapTcpService>();
    service->startWorkThread(1);
    auto session = brynet::net::SyncConnectSession(argv[1], 
        atoi(argv[2]), 
        std::chrono::seconds(10),
        service,
        {brynet::net::AddSessionOption::WithMaxRecvBufferSize(1024*1024)});
    auto client = createEchoClient(session);

    {
        gayrpc::core::RpcError error;
        EchoRequest request;
        request.set_message("sync echo test");

        // TODO::同步RPC可以简单的使用 future 实现timeout
        // Warining::同步RPC不能在RPC网络线程中调用(会导致无法发出请求或者Response)
        auto response = client->sync_Echo(request, error);

        std::cout << "echo result:" << error.failed() << std::endl;
        std::cout << "echo message:" << response.message() << std::endl;
    }

    {
        gayrpc::core::RpcError error;
        LoginRequest request;
        request.set_message("sync login test");

        auto response = client->sync_Login(request, error);

        std::cout << "login result:" << error.failed() << std::endl;
        std::cout << "login message:" << response.message() << std::endl;
    }

    return 0;
}
