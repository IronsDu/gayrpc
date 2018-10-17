#ifndef _GAY_RPC_UTILS_WRAPPER_H
#define _GAY_RPC_UTILS_WRAPPER_H

#include <functional>
#include <memory>
#include <exception>
#include <future>

#include <brynet/net/http/HttpService.h>
#include <brynet/net/Connector.h>
#include <brynet/net/http/HttpParser.h>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "GayRpcInterceptor.h"
#include "UtilsDataHandler.h"
#include "GayRpcTypeHandler.h"
#include "UtilsInterceptor.h"
#include "GayRpcService.h"

namespace utils_wrapper {
    template<typename RpcServiceType>
    using ServiceCreator = std::function<std::shared_ptr<RpcServiceType>(gayrpc::core::ServiceContext)>;

    using ClaimEventLoopFunctor = std::function<brynet::net::EventLoop::PTR()>;

    template<typename RpcClientType>
    using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

    template<typename RpcServiceType>
    static void OnHTTPConnectionEnter(const brynet::net::HttpSession::PTR &httpSession,
        ServiceCreator<RpcServiceType> serverCreator,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor *userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback) {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

        httpSession->setHttpCallback([=](const brynet::net::HTTPParser &httpParser,
            const brynet::net::HttpSession::PTR &session) {
            // 模拟构造一个RpcMeta，然后将POST body反序列化为RpcRequest对象，以此调用RPC
            gayrpc::core::RpcMeta meta;
            const auto &path = httpParser.getPath();
            meta.mutable_request_info()->set_strmethod(path.substr(1, path.size() - 1));
            meta.mutable_request_info()->set_expect_response(true);
            meta.set_encoding(gayrpc::core::RpcMeta::JSON);

            brynet::net::EventLoop::PTR handleEventLoop;
            if (claimEventLoopCallback != nullptr) {
                handleEventLoop = claimEventLoopCallback();
            }
            if (handleEventLoop != nullptr) {
                handleEventLoop->pushAsyncProc([=]() {
                    rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
                });
            } else {
                rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
            }
        });

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = utils_interceptor::withProtectedCall();
        if (userInboundInterceptor != nullptr) {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(
            utils_interceptor::withProtectedCall(),
            utils_interceptor::withHttpSessionSender(httpSession));
        if (userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        gayrpc::core::ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);
        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void StartHttpRpcServer(const brynet::net::TcpService::PTR &service,
        const brynet::net::ListenThread::PTR &listenThread,
        const std::string &ip, int port, ServiceCreator<RpcServiceType> serverCreator,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor &userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback, int packetLimit) {
        listenThread->startListen(false, ip, port, [=](brynet::net::TcpSocket::PTR socket) {
            auto enterCallback = [=](const brynet::net::DataSocket::PTR &session) {
                brynet::net::HttpService::setup(session, [=](const brynet::net::HttpSession::PTR &httpSession) {
                    OnHTTPConnectionEnter(httpSession, serverCreator, userInboundInterceptor, userOutBoundInterceptor,
                        claimEventLoopCallback);
                });
            };
            socket->SocketNodelay();
            service->addDataSocket(std::move(socket),
                brynet::net::TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        });
    }

    template<typename RpcServiceType>
    static void OnBinaryConnectionEnter(const brynet::net::DataSocket::PTR &session,
        const ServiceCreator<RpcServiceType> &serverCreator,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor &userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback,
        std::chrono::milliseconds heartBeat) {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

        session->setDataCallback([=](const char *buffer,
            size_t len) {
            // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
            brynet::net::EventLoop::PTR handleEventLoop;
            if (claimEventLoopCallback != nullptr) {
                handleEventLoop = claimEventLoopCallback();
            }
            return dataHandle(rpcHandlerManager, buffer, len, handleEventLoop);
        });
        session->setHeartBeat(heartBeat);

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = utils_interceptor::withProtectedCall();
        if (userInboundInterceptor != nullptr) {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(
            utils_interceptor::withProtectedCall(),
            utils_interceptor::withSessionSender(session));
        if (userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        gayrpc::core::ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);

        session->setDisConnectCallback([=](const brynet::net::DataSocket::PTR &session) {
            service->onClose();
        });

        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void StartBinaryRpcServer(const brynet::net::TcpService::PTR &service,
        const brynet::net::ListenThread::PTR &listenThread,
        const std::string &ip, int port,
        const ServiceCreator<RpcServiceType> &serverCreator,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor &userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback, 
        int packetLimit, 
        std::chrono::milliseconds heartBeat) {
        listenThread->startListen(false, ip, port, [=](brynet::net::TcpSocket::PTR socket) {
            auto enterCallback = [=](const brynet::net::DataSocket::PTR &session) {
                OnBinaryConnectionEnter(session,
                    serverCreator,
                    userInboundInterceptor,
                    userOutBoundInterceptor,
                    claimEventLoopCallback,
                    heartBeat);
            };

            socket->SocketNodelay();
            service->addDataSocket(std::move(socket),
                brynet::net::TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        });
    }

    template<typename RpcClientType>
    static void OnBinaryRpcClient(const brynet::net::DataSocket::PTR &session,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor &userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback,
        const RpcClientCallback<RpcClientType> &callback,
        std::chrono::milliseconds heartBeat) {
        auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
        session->setDataCallback([=](const char *buffer,
            size_t len) {
            brynet::net::EventLoop::PTR handleEventLoop;
            if (claimEventLoopCallback != nullptr) {
                handleEventLoop = claimEventLoopCallback();
            }
            return dataHandle(rpcHandlerManager, buffer, len, handleEventLoop);
        });
        session->setHeartBeat(heartBeat);

        // 入站拦截器
        gayrpc::core::UnaryServerInterceptor inboundInterceptor = utils_interceptor::withProtectedCall();
        if (userInboundInterceptor != nullptr) {
            inboundInterceptor = gayrpc::utils::makeInterceptor(inboundInterceptor, userInboundInterceptor);
        }
        // 出站拦截器
        gayrpc::core::UnaryServerInterceptor outBoundInterceptor = gayrpc::utils::makeInterceptor(
            utils_interceptor::withProtectedCall(),
            utils_interceptor::withSessionSender(session),
            utils_interceptor::withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));
        if (userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = gayrpc::utils::makeInterceptor(outBoundInterceptor, userOutBoundInterceptor);
        }

        // 注册RPC客户端
        auto client = RpcClientType::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        callback(client);
    }

    template<typename RpcClientType>
    static void AsyncCreateRpcClient(const brynet::net::TcpService::PTR &service,
        const brynet::net::AsyncConnector::PTR &connector, const std::string &ip,
        int port, std::chrono::milliseconds timeout,
        const gayrpc::core::UnaryServerInterceptor &userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor &userOutBoundInterceptor,
        const ClaimEventLoopFunctor &claimEventLoopCallback,
        const RpcClientCallback<RpcClientType> &callback,
        const brynet::net::AsyncConnector::FAILED_CALLBACK &failedCallback,
        int packetLimit,
        std::chrono::milliseconds heartBeat) {
        connector->asyncConnect(ip, port, timeout, [=](brynet::net::TcpSocket::PTR socket) {
            auto enterCallback = [=](const brynet::net::DataSocket::PTR &session) {
                OnBinaryRpcClient<RpcClientType>(session,
                    userInboundInterceptor,
                    userOutBoundInterceptor,
                    claimEventLoopCallback,
                    callback,
                    heartBeat);
            };

            socket->SocketNodelay();
            service->addDataSocket(std::move(socket),
                brynet::net::TcpService::AddSocketOption::WithEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(packetLimit));
        }, failedCallback);
    }

    template<typename RpcClientType>
    static std::shared_ptr<RpcClientType> SyncCreateRpcClient(const brynet::net::TcpService::PTR& service,
        brynet::net::AsyncConnector::PTR connector, 
        const std::string& ip, 
        int port, 
        std::chrono::milliseconds timeout,
        const gayrpc::core::UnaryServerInterceptor& userInboundInterceptor,
        const gayrpc::core::UnaryServerInterceptor& userOutBoundInterceptor,
        const ClaimEventLoopFunctor& calcimEventLoopCallback, 
        int packetLimit,
        std::chrono::milliseconds heartBeat)
    {
        auto rpcClientPromise = std::make_shared<std::promise<std::shared_ptr<RpcClientType>>>();

        RpcClientCallback<RpcClientType> callback = [=](std::shared_ptr<RpcClientType> rpcClient) {
            rpcClientPromise->set_value(rpcClient);
        };

        AsyncCreateRpcClient<RpcClientType>(service, connector, ip, port, timeout, 
            userInboundInterceptor, userOutBoundInterceptor, calcimEventLoopCallback, 
            callback, 
            [=]() {
                rpcClientPromise->set_value(nullptr);
            },
            packetLimit, heartBeat);

        auto future = rpcClientPromise->get_future();
        if (future.wait_for(timeout) != std::future_status::ready)
        {
            return nullptr;
        }

        return future.get();
    }
}

#endif