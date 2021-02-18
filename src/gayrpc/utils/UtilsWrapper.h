#pragma once

#include <gayrpc/core/GayRpcInterceptor.h>
#include <gayrpc/core/GayRpcService.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/utils/UtilsInterceptor.h>

#include <brynet/net/AsyncConnector.hpp>
#include <brynet/net/http/HttpParser.hpp>
#include <brynet/net/http/HttpService.hpp>
#include <brynet/net/wrapper/ConnectionBuilder.hpp>
#include <brynet/net/wrapper/ServiceBuilder.hpp>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <utility>

namespace gayrpc::utils {

using namespace gayrpc::core;
using namespace brynet::net;

using ServiceCreator = std::function<std::shared_ptr<BaseService>(ServiceContext&&)>;

template<typename RpcClientType>
using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

static void OnBinaryConnectionEnter(const brynet::net::TcpConnection::Ptr& session,
                                    const std::vector<ServiceCreator>& serverCreators,
                                    const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                                    std::vector<UnaryServerInterceptor> userOutBoundInterceptor)
{
    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
    std::vector<std::function<void(void)>> closedCallbacks;

    session->setDataCallback([=](brynet::base::BasePacketReader& reader) {
        // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, reader);
    });

    for (const auto& serverCreator : serverCreators)
    {
        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (!userInBoundInterceptor.empty())
        {
            inboundInterceptor = makeInterceptor(userInBoundInterceptor);
        }
        // 出站拦截器
        userOutBoundInterceptor.emplace_back(gayrpc::utils::withSessionBinarySender(session));
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

        ServiceContext serviceContext(rpcHandlerManager,
                                      std::move(inboundInterceptor),
                                      std::move(outBoundInterceptor));
        auto service = serverCreator(std::move(serviceContext));
        closedCallbacks.emplace_back([=]() {
            service->onClose();
        });
        service->install();
    }
    session->setDisConnectCallback([=](const brynet::net::TcpConnection::Ptr& session) {
        for (const auto& callback : closedCallbacks)
        {
            callback();
        }
    });
}

static void OnHTTPConnectionEnter(const brynet::net::http::HttpSession::Ptr& httpSession,
                                  brynet::net::http::HttpSessionHandlers& handlers,
                                  const std::vector<ServiceCreator>& serverCreators,
                                  const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                                  std::vector<UnaryServerInterceptor> userOutBoundInterceptor)
{
    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();

    handlers.setHttpCallback([=](const brynet::net::http::HTTPParser& httpParser,
                                 const brynet::net::http::HttpSession::Ptr& session) {
        gayrpc::protocol::http::handleHttpPacket(rpcHandlerManager, httpParser, session);
    });

    for (const auto& serverCreator : serverCreators)
    {
        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (!userInBoundInterceptor.empty())
        {
            inboundInterceptor = makeInterceptor(userInBoundInterceptor);
        }
        // 出站拦截器
        userOutBoundInterceptor.emplace_back(withProtectedCall());
        userOutBoundInterceptor.emplace_back(withHttpSessionSender(httpSession));
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

        ServiceContext serviceContext(rpcHandlerManager, std::move(inboundInterceptor), std::move(outBoundInterceptor));
        auto service = serverCreator(std::move(serviceContext));
        service->install();
    }
}

class BuildInterceptor final
{
public:
    explicit BuildInterceptor(std::vector<UnaryServerInterceptor>* interceptors)
        : mInterceptors(interceptors)
    {
    }

    void addInterceptor(const UnaryServerInterceptor& interceptor)
    {
        mInterceptors->push_back(interceptor);
    }

private:
    std::vector<UnaryServerInterceptor>* mInterceptors;
};

enum class TransportType
{
    Binary,
    HTTP,
};

class TransportTypeConfig final
{
public:
    explicit TransportTypeConfig(TransportType type)
        : mType(type)
    {}

    void setType(TransportType type)
    {
        mType = type;
    }

    auto getType() const
    {
        return mType;
    }

private:
    TransportType mType;
};

class BuildTransportType final
{
public:
    explicit BuildTransportType(TransportTypeConfig* config)
        : mConfig(config)
    {}

    void setType(TransportType type)
    {
        mConfig->setType(type);
    }

private:
    TransportTypeConfig* mConfig;
};

using InterceptorBuilder = std::function<void(BuildInterceptor)>;

class ServiceBuilder
{
public:
    using Ptr = std::shared_ptr<ServiceBuilder>;
    using BuildTransportTypeSet = std::function<void(BuildTransportType)>;

    ServiceBuilder()
        : mTransportTypeConfig(TransportType::Binary)
    {}
    virtual ~ServiceBuilder() = default;

    ServiceBuilder& WithService(TcpService::Ptr service)
    {
        mBuilder.WithService(std::move(service));
        return *this;
    }

    ServiceBuilder& WithAddr(bool ipV6, std::string ip, size_t port)
    {
        mBuilder.WithAddr(ipV6, std::move(ip), port);
        return *this;
    }

    ServiceBuilder& WithReusePort()
    {
        mBuilder.WithReusePort();
        return *this;
    }

    ServiceBuilder& AddSocketProcess(const ListenThread::TcpSocketProcessCallback& callback)
    {
        mBuilder.AddSocketProcess(callback);
        return *this;
    }

    ServiceBuilder& WithMaxRecvBufferSize(size_t size)
    {
        mBuilder.WithMaxRecvBufferSize(size);
        return *this;
    }
#ifdef BRYNET_USE_OPENSSL
    ServiceBuilder& WithSSL(SSLHelper::Ptr sslHelper)
    {
        mBuilder.WithSSL(sslHelper);
        return *this;
    }
#endif

    ServiceBuilder& buildInboundInterceptor(const InterceptorBuilder& builder)
    {
        buildInterceptor(builder, mInboundInterceptors);
        return *this;
    }

    ServiceBuilder& buildOutboundInterceptor(const InterceptorBuilder& builder)
    {
        buildInterceptor(builder, mOutboundInterceptors);
        return *this;
    }

    ServiceBuilder& addServiceCreator(const ServiceCreator& creator)
    {
        mCreators.push_back(creator);
        return *this;
    }

    ServiceBuilder& configureTransportType(const BuildTransportTypeSet& builder)
    {
        BuildTransportType buildTransportType(&mTransportTypeConfig);
        builder(buildTransportType);
        return *this;
    }

    void asyncRun()
    {
        mBuilder.AddEnterCallback([creators = mCreators,
                                   inboundInterceptors = mInboundInterceptors,
                                   outboundInterceptors = mOutboundInterceptors,
                                   transportType = mTransportTypeConfig.getType()](const brynet::net::TcpConnection::Ptr& session) {
            switch (transportType)
            {
                case TransportType::Binary:
                    OnBinaryConnectionEnter(session,
                                            creators,
                                            inboundInterceptors,
                                            outboundInterceptors);
                    break;
                case TransportType::HTTP:
                    brynet::net::http::HttpService::setup(session,
                                                          [=](const brynet::net::http::HttpSession::Ptr& httpSession,
                                                              brynet::net::http::HttpSessionHandlers& handlers) {
                                                              OnHTTPConnectionEnter(httpSession,
                                                                                    handlers,
                                                                                    creators,
                                                                                    inboundInterceptors,
                                                                                    outboundInterceptors);
                                                          });
                    break;
                default:
                    throw std::runtime_error(
                            std::string("not support transport type:") + std::to_string(static_cast<int>(transportType)));
            }
        });

        mBuilder.asyncRun();
    }

private:
    static void buildInterceptor(const InterceptorBuilder& builder,
                                 std::vector<UnaryServerInterceptor>& result)
    {
        BuildInterceptor buildInterceptor(&result);
        builder(buildInterceptor);
    }

private:
    std::vector<UnaryServerInterceptor> mInboundInterceptors;
    std::vector<UnaryServerInterceptor> mOutboundInterceptors;
    std::vector<ServiceCreator> mCreators;
    TransportTypeConfig mTransportTypeConfig;
    brynet::net::wrapper::ListenerBuilder mBuilder;
};

template<typename RpcClientType>
static void OnBinaryRpcClient(const brynet::net::TcpConnection::Ptr& session,
                              const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                              std::vector<UnaryServerInterceptor> userOutBoundInterceptor,
                              const RpcClientCallback<RpcClientType>& callback)
{
    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
    session->setDataCallback([=](brynet::base::BasePacketReader& reader) {
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, reader);
    });

    // 入站拦截器
    UnaryServerInterceptor inboundInterceptor = makeInterceptor();
    if (!userInBoundInterceptor.empty())
    {
        inboundInterceptor = makeInterceptor(userInBoundInterceptor);
    }
    // 出站拦截器
    userOutBoundInterceptor.emplace_back(gayrpc::utils::withSessionBinarySender(session));
    UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

    // 注册RPC客户端
    auto client = RpcClientType::Create(rpcHandlerManager,
                                        std::move(inboundInterceptor),
                                        std::move(outBoundInterceptor));
    client->setNetworkThreadChecker([session]() {
        return session->getEventLoop()->isInLoopThread();
    });
    callback(client);
}

class ClientBuilder
{
public:
    ClientBuilder& WithService(TcpService::Ptr service)
    {
        mBuilder.WithService(std::move(service));
        return *this;
    }
    virtual ~ClientBuilder() = default;

    ClientBuilder& WithConnector(AsyncConnector::Ptr connector)
    {
        mBuilder.WithConnector(std::move(connector));
        return *this;
    }

    ClientBuilder& WithAddr(std::string ip, size_t port)
    {
        mBuilder.WithAddr(std::move(ip), port);
        return *this;
    }

    ClientBuilder& WithTimeout(std::chrono::nanoseconds timeout)
    {
        mBuilder.WithTimeout(timeout);
        return *this;
    }

    ClientBuilder& AddSocketProcessCallback(const brynet::net::wrapper::ProcessTcpSocketCallback& callback)
    {
        mBuilder.AddSocketProcessCallback(callback);
        return *this;
    }

    ClientBuilder& WithFailedCallback(brynet::net::wrapper::FailedCallback callback)
    {
        mBuilder.WithFailedCallback(std::move(callback));
        return *this;
    }

    ClientBuilder& WithMaxRecvBufferSize(size_t size)
    {
        mBuilder.WithMaxRecvBufferSize(size);
        return *this;
    }

#ifdef BRYNET_USE_OPENSSL
    ClientBuilder& WithSSL()
    {
        mBuilder.WithSSL();
        return *this;
    }
#endif

    ClientBuilder& buildInboundInterceptor(const InterceptorBuilder& builder)
    {
        buildInterceptor(builder, mInboundInterceptors);
        return *this;
    }

    ClientBuilder& buildOutboundInterceptor(const InterceptorBuilder& builder)
    {
        buildInterceptor(builder, mOutboundInterceptors);
        return *this;
    }

    template<typename RpcClientType>
    void asyncConnect(const RpcClientCallback<RpcClientType>& callback)
    {
        auto inboundInterceptors = mInboundInterceptors;
        auto outboundInterceptors = mOutboundInterceptors;
        auto enterCallback = [=](const brynet::net::TcpConnection::Ptr& session) {
            OnBinaryRpcClient<RpcClientType>(session,
                                             inboundInterceptors,
                                             outboundInterceptors,
                                             callback);
        };

        auto builder = mBuilder;
        builder.AddEnterCallback(enterCallback);
        builder.asyncConnect();
    }

protected:
    static void buildInterceptor(const InterceptorBuilder& builder,
                                 std::vector<UnaryServerInterceptor>& result)
    {
        BuildInterceptor buildInterceptor(&result);
        builder(buildInterceptor);
    }

private:
    std::vector<UnaryServerInterceptor> mInboundInterceptors;
    std::vector<UnaryServerInterceptor> mOutboundInterceptors;
    brynet::net::wrapper::ConnectionBuilder mBuilder;
};

}// namespace gayrpc::utils
