#pragma once

#include <functional>
#include <memory>
#include <exception>
#include <future>

#include <brynet/net/http/HttpService.h>
#include <brynet/net/Connector.h>
#include <brynet/net/http/HttpParser.h>

#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcInterceptor.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/GayRpcService.h>
#include <gayrpc/utils/UtilsInterceptor.h>
#include <gayrpc/protocol/BinaryProtocol.h>

namespace gayrpc { namespace utils {

    using namespace gayrpc::core;
    using namespace brynet::net;

    template<typename RpcServiceType>
    using ServiceCreator = std::function<std::shared_ptr<RpcServiceType>(ServiceContext)>;
    using ClaimEventLoopFunctor = std::function<brynet::net::EventLoop::Ptr()>;

    struct RpcConfig final
    {
        using AddRpcConfigFunc = std::function<void(RpcConfig& config)>;

    public:
        static AddRpcConfigFunc WithInboundInterceptor(UnaryServerInterceptor userInBoundInterceptor)
        {
            return [=](RpcConfig& config) {
                config.userInBoundInterceptor = userInBoundInterceptor;
            };
        }

        static AddRpcConfigFunc WithOutboundInterceptor(UnaryServerInterceptor userOutBoundInterceptor)
        {
            return [=](RpcConfig& config) {
                config.userOutBoundInterceptor = userOutBoundInterceptor;
            };
        }

    public:
        UnaryServerInterceptor      userInBoundInterceptor;
        UnaryServerInterceptor      userOutBoundInterceptor;
    };

    template<typename RpcClientType>
    using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

    template<typename RpcServiceType>
    static void OnHTTPConnectionEnter(const brynet::net::http::HttpSession::Ptr& httpSession,
        const ServiceCreator<RpcServiceType>& serverCreator,
        const RpcConfig& config)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();

        httpSession->setHttpCallback([=](const brynet::net::http::HTTPParser &httpParser,
            const brynet::net::http::HttpSession::Ptr &session) {
            gayrpc::protocol::http::handleHttpPacket(rpcHandlerManager, httpParser, session);
        });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = withProtectedCall();
        if (config.userInBoundInterceptor != nullptr) {
            inboundInterceptor = makeInterceptor(inboundInterceptor, config.userInBoundInterceptor);
        }
        // 出站拦截器
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(
            withProtectedCall(),
            withHttpSessionSender(httpSession));
        if (config.userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = makeInterceptor(outBoundInterceptor, config.userOutBoundInterceptor);
        }

        ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);
        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void OnBinaryConnectionEnter(const brynet::net::TcpConnection::Ptr& session,
        const ServiceCreator<RpcServiceType>& serverCreator,
        const RpcConfig& config)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();

        session->setDataCallback([=](const char *buffer,
            size_t len) {
            // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
            return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, buffer, len);
        });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (config.userInBoundInterceptor != nullptr) {
            inboundInterceptor = makeInterceptor(inboundInterceptor, config.userInBoundInterceptor);
        }
        // 出站拦截器
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(
            gayrpc::utils::withSessionBinarySender(session));
        if (config.userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = makeInterceptor(outBoundInterceptor, config.userOutBoundInterceptor);
        }

        ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);

        session->setDisConnectCallback([=](const brynet::net::TcpConnection::Ptr &session) {
            service->onClose();
        });

        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static auto WrapTcpRpc(const brynet::net::TcpService::Ptr &service,
        const ServiceCreator<RpcServiceType>& serverCreator,
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc> socketOptions,
        const std::vector<RpcConfig::AddRpcConfigFunc>& configSettings)
    {
        RpcConfig config;
        for (const auto& setting : configSettings)
        {
            setting(config);
        }

        socketOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(
            [=](const brynet::net::TcpConnection::Ptr &session) {
                OnBinaryConnectionEnter(session, serverCreator, config);
            }));

        return [=](brynet::net::TcpSocket::Ptr socket) {
            service->addTcpConnection(std::move(socket), socketOptions);
        };
    }

    template<typename RpcServiceType>
    static auto WrapHttpRpc(
        const brynet::net::TcpService::Ptr& service,
        const ServiceCreator<RpcServiceType>& serverCreator,
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc> socketOptions,
        const std::vector<RpcConfig::AddRpcConfigFunc>& configSettings)
    {
        RpcConfig config;
        for (const auto& setting : configSettings)
        {
            setting(config);
        }

        socketOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(
            [=](const brynet::net::TcpConnection::Ptr &session) {
                brynet::net::http::HttpService::setup(session, [=](const brynet::net::http::HttpSession::Ptr &httpSession) {
                    OnHTTPConnectionEnter(httpSession, serverCreator, config);
                });
            }));

        return [=](brynet::net::TcpSocket::Ptr socket) {
            service->addTcpConnection(std::move(socket), socketOptions);
        };
    }

    template<typename RpcClientType>
    static void OnBinaryRpcClient(const brynet::net::TcpConnection::Ptr &session,
        const RpcConfig& config,
        const RpcClientCallback<RpcClientType> &callback)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
        session->setDataCallback([=](const char *buffer,
            size_t len) {
            return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, buffer, len);
        });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = withProtectedCall();
        if (config.userInBoundInterceptor != nullptr) {
            inboundInterceptor = makeInterceptor(inboundInterceptor, config.userInBoundInterceptor);
        }
        // 出站拦截器
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(
            withProtectedCall(),
            gayrpc::utils::withSessionBinarySender(session),
            withTimeoutCheck(session->getEventLoop(), rpcHandlerManager));
        if (config.userOutBoundInterceptor != nullptr) {
            outBoundInterceptor = makeInterceptor(outBoundInterceptor, config.userOutBoundInterceptor);
        }

        // 注册RPC客户端
        auto client = RpcClientType::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        callback(client);
    }

    template<typename RpcClientType>
    static void AsyncCreateRpcClient(const TcpService::Ptr& service,
        const AsyncConnector::Ptr &connector,
        std::vector<AsyncConnector::ConnectOptions::ConnectOptionFunc> connectOptions,
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc> socketOptions,
        const std::vector<RpcConfig::AddRpcConfigFunc>& configSettings, 
        const RpcClientCallback<RpcClientType>& callback)
    {
        auto enterCallback = [=](brynet::net::TcpSocket::Ptr socket) mutable {
            RpcConfig config;
            for (const auto& setting : configSettings)
            {
                setting(config);
            }

            auto enterCallback = [=](const brynet::net::TcpConnection::Ptr &session) {
                OnBinaryRpcClient<RpcClientType>(session, config, callback);
            };

            socket->setNodelay();
            socketOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(enterCallback));
            service->addTcpConnection(std::move(socket), socketOptions);
        };
        connectOptions.push_back(AsyncConnector::ConnectOptions::WithCompletedCallback(enterCallback));

        connector->asyncConnect(connectOptions);
    }

    template<typename RpcClientType>
    static std::shared_ptr<RpcClientType> SyncCreateRpcClient(const brynet::net::TcpService::Ptr& service,
        brynet::net::AsyncConnector::Ptr connector,
        std::vector<AsyncConnector::ConnectOptions::ConnectOptionFunc> connectOptions,
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc> socketOptions,
        const std::vector<RpcConfig::AddRpcConfigFunc>& configSettings)
    {
        auto rpcClientPromise = std::make_shared<std::promise<std::shared_ptr<RpcClientType>>>();

        RpcClientCallback<RpcClientType> callback = [=](std::shared_ptr<RpcClientType> rpcClient) {
            rpcClientPromise->set_value(rpcClient);
        };

        AsyncCreateRpcClient<RpcClientType>(service, connector, 
            connectOptions,
            socketOptions,
            configSettings,
            callback);

        return rpcClientPromise->get_future().get();
    }

} }