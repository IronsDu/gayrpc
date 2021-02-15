#pragma once

#include <gayrpc/core/GayRpcInterceptor.h>
#include <gayrpc/core/GayRpcService.h>
#include <gayrpc/core/GayRpcType.h>
#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <gayrpc/protocol/BinaryProtocol.h>
#include <gayrpc/utils/UtilsInterceptor.h>

#include <bsio/net/TcpConnector.hpp>
#include <bsio/net/http/HttpParser.hpp>
#include <bsio/net/http/HttpService.hpp>
#include <bsio/net/wrapper/AcceptorBuilder.hpp>
#include <bsio/net/wrapper/ConnectorBuilder.hpp>
#include <bsio/net/wrapper/HttpAcceptorBuilder.hpp>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <utility>

namespace gayrpc::utils {

using namespace gayrpc::core;
using namespace bsio::net;

using ServiceCreator = std::function<std::shared_ptr<BaseService>(ServiceContext&&)>;

template<typename RpcClientType>
using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

static void OnBinaryConnectionEnter(const bsio::net::TcpSession::Ptr& session,
                                    const std::vector<ServiceCreator>& serverCreators,
                                    const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                                    std::vector<UnaryServerInterceptor> userOutBoundInterceptor)
{
    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
    std::vector<std::function<void(void)>> closedCallbacks;
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

    session->setDataHandler([=](const bsio::net::TcpSession::Ptr& session, bsio::base::BasePacketReader& reader) {
        (void) session;
        // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
        return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, reader);
    });
    session->setClosedHandler([=](const bsio::net::TcpSession::Ptr& session) {
        for (const auto& callback : closedCallbacks)
        {
            callback();
        }
    });
}

static void OnHTTPConnectionEnter(bsio::net::TcpSession::Ptr session,
                                  const std::vector<ServiceCreator>& serverCreators,
                                  const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                                  std::vector<UnaryServerInterceptor> userOutBoundInterceptor)
{
    using namespace bsio::net::http;

    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
    bsio::net::wrapper::internal::setupHttpSession(
            session,
            [=](const HttpSession::Ptr& httpSession) mutable {
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

                    ServiceContext serviceContext(rpcHandlerManager,
                                                  std::move(inboundInterceptor),
                                                  std::move(outBoundInterceptor));
                    auto service = serverCreator(std::move(serviceContext));
                    service->install();
                }
            },
            [=](const HTTPParser& httpParser, const HttpSession::Ptr& session) {
                gayrpc::protocol::http::handleHttpPacket(rpcHandlerManager, httpParser, session);
            },
            [](const HttpSession::Ptr&,
               WebSocketFormat::WebSocketFrameType opcode,
               const std::string& payload) {
                //TODO::support websocket
            },
            nullptr);
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

    virtual ~ServiceBuilder() = default;

    ServiceBuilder()
        : mTransportTypeConfig(TransportType::Binary)
    {}

    ServiceBuilder& WithAcceptor(TcpAcceptor::Ptr acceptor) noexcept
    {
        mBuilder.WithAcceptor(std::move(acceptor));
        return *this;
    }

    ServiceBuilder& WithRecvBufferSize(size_t size) noexcept
    {
        mBuilder.WithRecvBufferSize(size);
        return *this;
    }

    ServiceBuilder& AddSocketProcessingHandler(SocketProcessingHandler handler) noexcept
    {
        mBuilder.AddSocketProcessingHandler(std::move(handler));
        return *this;
    }

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
        mBuilder.WithSessionOptionBuilder(
                [creators = mCreators,
                 inboundInterceptors = mInboundInterceptors,
                 outboundInterceptors = mOutboundInterceptors,
                 transportType = mTransportTypeConfig.getType()](bsio::net::wrapper::SessionOptionBuilder& builder) {
                    using namespace bsio::net::http;

                    switch (transportType)
                    {
                        case TransportType::Binary:
                        {
                            builder.AddEnterCallback([=](const bsio::net::TcpSession::Ptr& session) {
                                OnBinaryConnectionEnter(session,
                                                        creators,
                                                        inboundInterceptors,
                                                        outboundInterceptors);
                            });
                        }
                        break;
                        case TransportType::HTTP:
                            builder.AddEnterCallback([=](bsio::net::TcpSession::Ptr session) {
                                OnHTTPConnectionEnter(std::move(session), creators, inboundInterceptors, outboundInterceptors);
                            });
                            break;
                        default:
                            throw std::runtime_error(
                                    std::string("not support transport type:") + std::to_string(static_cast<int>(transportType)));
                    }
                });
        mBuilder.start();
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

    bsio::net::wrapper::TcpSessionAcceptorBuilder mBuilder;
};

template<typename RpcClientType>
static void OnBinaryRpcClient(const bsio::net::TcpSession::Ptr& session,
                              const std::vector<UnaryServerInterceptor>& userInBoundInterceptor,
                              std::vector<UnaryServerInterceptor> userOutBoundInterceptor,
                              const RpcClientCallback<RpcClientType>& callback)
{
    auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
    session->setDataHandler([=](const TcpSession::Ptr& session, bsio::base::BasePacketReader& reader) {
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
        //return session->getEventLoop()->isInLoopThread();
        return false;
    });
    callback(client);
}

class ClientBuilder
{
public:
    virtual ~ClientBuilder() = default;

    ClientBuilder& WithConnector(TcpConnector connector) noexcept
    {
        mBuilder.WithConnector(std::move(connector));
        return *this;
    }

    ClientBuilder& WithEndpoint(asio::ip::tcp::endpoint endpoint) noexcept
    {
        mBuilder.WithEndpoint(std::move(endpoint));
        return *this;
    }

    ClientBuilder& WithTimeout(std::chrono::nanoseconds timeout) noexcept
    {
        mBuilder.WithTimeout(timeout);
        return *this;
    }

    ClientBuilder& WithRecvBufferSize(size_t size) noexcept
    {
        mBuilder.WithRecvBufferSize(size);
        return *this;
    }

    ClientBuilder& WithFailedHandler(SocketFailedConnectHandler handler) noexcept
    {
        mBuilder.WithFailedHandler(std::move(handler));
        return *this;
    }

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
        auto enterCallback = [inboundInterceptors = mInboundInterceptors,
                              outboundInterceptors = mOutboundInterceptors,
                              callback = callback](bsio::net::TcpSession::Ptr session) {
            OnBinaryRpcClient<RpcClientType>(session,
                                             inboundInterceptors,
                                             outboundInterceptors,
                                             callback);
        };

        mBuilder.AddEnterCallback(enterCallback);
        mBuilder.asyncConnect();
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
    bsio::net::wrapper::TcpSessionConnectorBuilder mBuilder;
};

}// namespace gayrpc::utils
