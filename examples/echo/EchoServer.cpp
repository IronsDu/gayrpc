#include <iostream>
#include <mutex>
#include <atomic>
#include <map>

#include <brynet/net/SocketLibFunction.h>
#include <brynet/net/EventLoop.h>
#include <brynet/net/TCPService.h>
#include <brynet/net/ListenThread.h>
#include <brynet/net/Socket.h>
#include <brynet/utils/packet.h>
#include <brynet/net/http/HttpService.h>
#include <brynet/net/http/HttpFormat.h>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "OpPacket.h"
#include "UtilsDataHandler.h"
#include "GayRpcInterceptor.h"
#include "UtilsInterceptor.h"

#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace gayrpc::core;
using namespace dodo::test;

std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    MyService(const std::shared_ptr<EchoServerClient>& client)
        :
        mClient(client)
    {
    }

    void Echo(const EchoRequest& request, 
        const EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response);

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
        const LoginReply::PTR& replyObj) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response);
    }

private:
    std::shared_ptr<EchoServerClient>   mClient;
};

static void counter(const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next)
{
    count++;
    next(meta, message);
}

static void onNormalTCPConnection(const DataSocket::PTR& session)
{
    std::cout << "connection enter" << std::endl;

    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataCallback([rpcHandlerManager](const char* buffer,
        size_t len) {
        // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
        return dataHandle(rpcHandlerManager, buffer, len);
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), counter);

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::utils::makeInterceptor(withSessionSender(std::weak_ptr<DataSocket>(session)),
        withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));

    // 创建客户端
    auto client = EchoServerClient::Create(rpcHandlerManager, outBoundInterceptor, inboundInterceptor);

    // 创建服务
    auto rpcServer = std::make_shared<MyService>(client);
    EchoServerService::Install(rpcHandlerManager, rpcServer, inboundInterceptor, outBoundInterceptor);

    session->setDisConnectCallback([rpcServer](const DataSocket::PTR& session) {
        std::cout << "close session" << std::endl;
        rpcServer->onClose();
    });
}

auto withHttpSessionSender(const HttpSession::PTR& httpSession)
{
    return [httpSession](const gayrpc::core::RpcMeta& meta,
        const google::protobuf::Message& message,
        const gayrpc::core::UnaryHandler& next) {

        std::string jsonMsg;
        google::protobuf::util::MessageToJsonString(message, &jsonMsg);

        brynet::net::HttpResponse httpResponse;
        httpResponse.setStatus(HttpResponse::HTTP_RESPONSE_STATUS::OK);
        httpResponse.setContentType("application/json");
        httpResponse.setBody(jsonMsg.c_str());

        auto result = httpResponse.getResult();
        httpSession->send(result.c_str(), result.size(), nullptr);

        httpSession->postShutdown();

        next(meta, message);
    };
}

// TODO::抽线此函数和创建HTTP服务的代码，用于快速构建HTTP-RPC
static void onHTTPConnection(const HttpSession::PTR& httpSession)
{
    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

    httpSession->setHttpCallback([rpcHandlerManager](const HTTPParser& httpParser,
        const HttpSession::PTR& session) {
        // 模拟构造一个RpcMeta，然后将POST body反序列化为RpcRequest对象，以此调用RPC
        RpcMeta meta;
        auto path = httpParser.getPath();
        meta.mutable_request_info()->set_strmethod(path.substr(1, path.size()-1));
        meta.mutable_request_info()->set_expect_response(true);
        meta.set_encoding(RpcMeta::JSON);
        rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall());

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), withHttpSessionSender(httpSession));

    // 创建服务
    auto rpcServer = std::make_shared<MyService>(nullptr);
    EchoServerService::Install(rpcHandlerManager, rpcServer, inboundInterceptor, outBoundInterceptor);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: <listen port>\n");
        exit(-1);
    }

    auto server = TcpService::Create();

    // TODO::抽象下面开启HTTP服务的代码
    // 开启HTTP监听（提供RPC)
    // Test:curl -d '{"message":"Hello, world!"}' http://localhost:8080/dodo.test.EchoServer.echo
    auto httpListenThread = ListenThread::Create();
    httpListenThread->startListen(false, "0.0.0.0", 8080, [server](TcpSocket::PTR socket) {
        auto enterCallback = [](const DataSocket::PTR& session) {
            HttpService::setup(session, [](const HttpSession::PTR& httpSession) {
                onHTTPConnection(httpSession);
            });
        };
        server->addDataSocket(std::move(socket),
            TcpService::AddSocketOption::WithEnterCallback(enterCallback),
            TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024));
    });

    // 开启普通TCP监听，采用二进制协议（提供RPC）
    auto listenThread = ListenThread::Create();
    listenThread->startListen(
        false, 
        "0.0.0.0", 
        atoi(argv[1]), 
        [=](TcpSocket::PTR socket){
            socket->SocketNodelay();
            server->addDataSocket(std::move(socket), 
                brynet::net::TcpService::AddSocketOption::WithEnterCallback(onNormalTCPConnection),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024));
        });

    server->startWorkerThread(std::thread::hardware_concurrency());

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);
    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
