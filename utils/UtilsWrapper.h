#ifndef _GAY_RPC_UTILS_WRAPPER_H
#define _GAY_RPC_UTILS_WRAPPER_H

#include <functional>
#include <memory>
#include <exception>

#include <brynet/net/http/HttpService.h>
#include <brynet/net/Connector.h>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"
#include "GayRpcTypeHandler.h"
#include "UtilsInterceptor.h"
#include "GayRpcService.h"

namespace utils_wrapper
{
    template<typename RpcServiceType>
    using ServiceCreator = std::function < std::shared_ptr<RpcServiceType>(gayrpc::core::ServiceContext)>;

    using ClaimEventLoopFunctor = std::function< brynet::net::EventLoop::PTR()>;
    
    template<typename RpcClientType>
    using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

    template<typename RpcServiceType>
    static void OnHTTPConnectionEnter(const brynet::net::HttpSession::PTR& httpSession, ServiceCreator<RpcServiceType> serverCreator,
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback)
    {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

        httpSession->setHttpCallback([=](const HTTPParser& httpParser,
            const HttpSession::PTR& session) {
            // 模拟构造一个RpcMeta，然后将POST body反序列化为RpcRequest对象，以此调用RPC
            RpcMeta meta;
            auto path = httpParser.getPath();
            meta.mutable_request_info()->set_strmethod(path.substr(1, path.size() - 1));
            meta.mutable_request_info()->set_expect_response(true);
            meta.set_encoding(RpcMeta::JSON);

            brynet::net::EventLoop::PTR handleEventLoop;
            if (calcimEventLoopCallback != nullptr)
            {
                handleEventLoop = calcimEventLoopCallback();
            }
            if (handleEventLoop != nullptr)
            {
                handleEventLoop->pushAsyncProc([=]() {
                    rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
                });
            }
            else
            {
                rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
            }
        });

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = withProtectedCall();
        if (userInboundInterceptor != nullptr)
        {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), withHttpSessionSender(httpSession));
        if (userOutBoundInterceptor != nullptr)
        {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        gayrpc::core::ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);
        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void StartHttpRpcServer(brynet::net::TcpService::PTR service, brynet::net::ListenThread::PTR listenThread,
        std::string ip, int port, ServiceCreator<RpcServiceType> serverCreator,
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback, int packetLimit)
    {
        listenThread->startListen(false, ip, port, [=](TcpSocket::PTR socket) {
            auto enterCallback = [=](const DataSocket::PTR& session) {
                HttpService::setup(session, [=](const HttpSession::PTR& httpSession) {
                    OnHTTPConnectionEnter(httpSession, serverCreator, userInboundInterceptor, userOutBoundInterceptor, calcimEventLoopCallback);
                });
            };
            socket->SocketNodelay();
            service->addDataSocket(std::move(socket),
                TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        });
    }

    template<typename RpcServiceType>
    static void OnBinaryConnectionEnter(const brynet::net::DataSocket::PTR& session, ServiceCreator<RpcServiceType> serverCreator,
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback)
    {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

        session->setDataCallback([=](const char* buffer,
            size_t len) {
            // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
            brynet::net::EventLoop::PTR handleEventLoop;
            if (calcimEventLoopCallback != nullptr)
            {
                handleEventLoop = calcimEventLoopCallback();
            }
            return dataHandle(rpcHandlerManager, buffer, len, handleEventLoop);
        });

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = withProtectedCall();
        if (userInboundInterceptor != nullptr)
        {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), withSessionSender(session));
        if (userOutBoundInterceptor != nullptr)
        {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        gayrpc::core::ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);

        session->setDisConnectCallback([=](const DataSocket::PTR& session) {
            service->onClose();
        });

        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void StartBinaryRpcServer(brynet::net::TcpService::PTR service, brynet::net::ListenThread::PTR listenThread,
        std::string ip, int port, ServiceCreator<RpcServiceType> serverCreator,
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback, int packetLimit)
    {
        listenThread->startListen(false, ip, port, [=](TcpSocket::PTR socket) {
            auto enterCallback = [=](const DataSocket::PTR& session) {
                OnBinaryConnectionEnter(session, serverCreator, userInboundInterceptor, userOutBoundInterceptor, calcimEventLoopCallback);
            };

            socket->SocketNodelay();
            service->addDataSocket(std::move(socket),
                brynet::net::TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        });
    }

    template<typename RpcClientType>
    static void OnBinaryRpcClient(const brynet::net::DataSocket::PTR& session, 
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback, RpcClientCallback<RpcClientType> callback)
    {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
        session->setDataCallback([=](const char* buffer,
            size_t len) {
            brynet::net::EventLoop::PTR handleEventLoop;
            if (calcimEventLoopCallback != nullptr)
            {
                handleEventLoop = calcimEventLoopCallback();
            }
            return dataHandle(rpcHandlerManager, buffer, len, handleEventLoop);
        });

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = withProtectedCall();
        if (userInboundInterceptor != nullptr)
        {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), 
            withSessionSender(session), withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));
        if (userOutBoundInterceptor != nullptr)
        {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        // 注册RPC客户端
        auto client = RpcClientType::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        callback(client);
    }

    template<typename RpcClientType>
    static void AsyncCreateRpcClient(brynet::net::TcpService::PTR service, brynet::net::AsyncConnector::PTR connector, std::string ip, int port, std::chrono::milliseconds timeout,
        gayrpc::core::UnaryServerInterceptor userInboundInterceptor, gayrpc::core::UnaryServerInterceptor userOutBoundInterceptor,
        ClaimEventLoopFunctor calcimEventLoopCallback, RpcClientCallback<RpcClientType> callback, brynet::net::AsyncConnector::FAILED_CALLBACK failedCallback,
        int packetLimit)
    {
        connector->asyncConnect(ip, port, timeout, [=](TcpSocket::PTR socket) {
            auto enterCallback = [=](const DataSocket::PTR& session) {
                OnBinaryRpcClient<RpcClientType>(session, userInboundInterceptor, userOutBoundInterceptor, calcimEventLoopCallback, callback);
            };

            socket->SocketNodelay();
            service->addDataSocket(std::move(socket), brynet::net::TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        }, failedCallback);
    }
}

#endif