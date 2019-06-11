#pragma once

#include <functional>
#include <memory>
#include <exception>
#include <future>

#include <brynet/net/http/HttpService.h>
#include <brynet/net/Connector.h>
#include <brynet/net/http/HttpParser.h>
#include <brynet/net/Wrapper.h>

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
    using ServiceCreator = std::function<std::shared_ptr<RpcServiceType>(ServiceContext&&)>;

    template<typename RpcClientType>
    using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

    template<typename RpcServiceType>
    static void OnBinaryConnectionEnter(const brynet::net::TcpConnection::Ptr& session,
        const ServiceCreator<RpcServiceType>& serverCreator,
        std::vector<UnaryServerInterceptor>  userInBoundInterceptor,
        std::vector<UnaryServerInterceptor>  userOutBoundInterceptor)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();

        session->setDataCallback([=](const char *buffer,
            size_t len) {
            // 二进制协议解析器,在其中调用rpcHandlerManager->handleRpcMsg进入RPC核心处理
            return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, buffer, len);
        });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (!userInBoundInterceptor.empty()) {
            inboundInterceptor = makeInterceptor(userInBoundInterceptor);
        }
        // 出站拦截器
        userOutBoundInterceptor.push_back(gayrpc::utils::withSessionBinarySender(session));
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

        ServiceContext serviceContext(rpcHandlerManager, std::move(inboundInterceptor), std::move(outBoundInterceptor));
        auto service = serverCreator(std::move(serviceContext));

        session->setDisConnectCallback([=](const brynet::net::TcpConnection::Ptr &session) {
            service->onClose();
        });

        RpcServiceType::Install(service);
    }

    template<typename RpcServiceType>
    static void OnHTTPConnectionEnter(const brynet::net::http::HttpSession::Ptr& httpSession,
        const ServiceCreator<RpcServiceType>& serverCreator,
        std::vector<UnaryServerInterceptor>  userInBoundInterceptor,
        std::vector<UnaryServerInterceptor>  userOutBoundInterceptor)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();

        httpSession->setHttpCallback([=](const brynet::net::http::HTTPParser & httpParser,
            const brynet::net::http::HttpSession::Ptr & session) {
                gayrpc::protocol::http::handleHttpPacket(rpcHandlerManager, httpParser, session);
            });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (!userInBoundInterceptor.empty()) {
            inboundInterceptor = makeInterceptor(userInBoundInterceptor);
        }
        // 出站拦截器	
        userOutBoundInterceptor.push_back(withProtectedCall());
        userOutBoundInterceptor.push_back(withHttpSessionSender(httpSession));
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

        ServiceContext serviceContext(rpcHandlerManager, std::move(inboundInterceptor), std::move(outBoundInterceptor));
        auto service = serverCreator(std::move(serviceContext));
        RpcServiceType::Install(service);
    }

    class BuildInterceptor final
    {
    public:
        BuildInterceptor(std::vector<UnaryServerInterceptor>* nterceptors)
        {
            mInterceptors = nterceptors;
        }

        void    addInterceptor(UnaryServerInterceptor interceptor)
        {
            mInterceptors->push_back(interceptor);
        }

    private:
        std::vector<UnaryServerInterceptor>*    mInterceptors;
    };

    enum class TransportType
    {
        Binary,
        HTTP,
    };

    class TransportTypeConfig final
    {
    public:
        TransportTypeConfig(TransportType type)
            :
            mType(type)
        {}

        void    setType(TransportType type)
        {
            mType = type;
        }

        auto    getType() const
        {
            return mType;
        }

    private:
        TransportType   mType;
    };

    class BuildTransportType final
    {
    public:
        BuildTransportType(TransportTypeConfig* config)
            :
            mConfig(config)
        {}

        void    setType(TransportType type)
        {
            mConfig->setType(type);
        }

    private:
        TransportTypeConfig* mConfig;
    };

    using InterceptorBuilder = std::function<void(BuildInterceptor)>;

    template<typename RpcServiceType>
    class ServiceBuilder : public wrapper::BaseListenerBuilder<ServiceBuilder<RpcServiceType>>
    {
    public:
        using Ptr = std::shared_ptr<ServiceBuilder<RpcServiceType>>;
        using BuildTransportTypeSet = std::function<void(BuildTransportType)>;

        ServiceBuilder()
            :
            mTransportTypeConfig(TransportType::Binary)
        {}

        ServiceBuilder<RpcServiceType>& buildInboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mInboundInterceptors);
            return *this;
        }

        ServiceBuilder<RpcServiceType>& buildOutboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mOutboundInterceptors);
            return *this;
        }

        ServiceBuilder<RpcServiceType>& configureCreator(ServiceCreator<RpcServiceType> creator)
        {
            mCreator = creator;
            return *this;
        }

        ServiceBuilder<RpcServiceType>&    configureTransportType(BuildTransportTypeSet builder)
        {
            BuildTransportType buildTransportType(&mTransportTypeConfig);
            builder(buildTransportType);
            return *this;
        }

        void    asyncRun()
        {
            auto connectionOptions = wrapper::BaseListenerBuilder<ServiceBuilder<RpcServiceType>>::getConnectionOptions();
            connectionOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(
                [creator = mCreator,
                inboundInterceptors = mInboundInterceptors,
                outboundInterceptors = mOutboundInterceptors,
                transportType = mTransportTypeConfig.getType()](const brynet::net::TcpConnection::Ptr & session) {
                    switch (transportType)
                    {
                    case TransportType::Binary:
                        OnBinaryConnectionEnter(session,
                            creator,
                            inboundInterceptors,
                            outboundInterceptors);
                        break;
                    case TransportType::HTTP:
                        brynet::net::http::HttpService::setup(session,
                            [=](const brynet::net::http::HttpSession::Ptr & httpSession) {
                                OnHTTPConnectionEnter(httpSession, 
                                    creator, 
                                    inboundInterceptors,
                                    outboundInterceptors);
                            });
                        break;
                    default:
                        throw std::runtime_error(
                            std::string("not support transport type:") 
                            + std::to_string(static_cast<int>(transportType)));
                        break;
                    }
                }));

            wrapper::BaseListenerBuilder<ServiceBuilder<RpcServiceType>>::asyncRun(connectionOptions);
        }

    private:
        void buildInterceptor(InterceptorBuilder builder, std::vector<UnaryServerInterceptor>& result)
        {
            BuildInterceptor buildInterceptor(&result);
            builder(buildInterceptor);
        }

    private:
        std::vector<UnaryServerInterceptor>     mInboundInterceptors;
        std::vector<UnaryServerInterceptor>     mOutboundInterceptors;
        ServiceCreator<RpcServiceType>          mCreator;
        TransportTypeConfig                     mTransportTypeConfig;
    };

    template<typename RpcClientType>
    static void OnBinaryRpcClient(const brynet::net::TcpConnection::Ptr &session,
        std::vector<UnaryServerInterceptor>  userInBoundInterceptor,
        std::vector<UnaryServerInterceptor>  userOutBoundInterceptor,
        const RpcClientCallback<RpcClientType> &callback)
    {
        auto rpcHandlerManager = std::make_shared<RpcTypeHandleManager>();
        session->setDataCallback([=](const char *buffer,
            size_t len) {
            return gayrpc::protocol::binary::binaryPacketHandle(rpcHandlerManager, buffer, len);
        });

        // 入站拦截器
        UnaryServerInterceptor inboundInterceptor = makeInterceptor();
        if (!userInBoundInterceptor.empty()) {
            inboundInterceptor = makeInterceptor(userInBoundInterceptor);
        }
        // 出站拦截器
        userOutBoundInterceptor.push_back(gayrpc::utils::withSessionBinarySender(session));
        UnaryServerInterceptor outBoundInterceptor = makeInterceptor(userOutBoundInterceptor);

        // 注册RPC客户端
        auto client = RpcClientType::Create(rpcHandlerManager, std::move(inboundInterceptor), std::move(outBoundInterceptor));
        callback(client);
    }

    class ClientBuilder : public wrapper::BaseConnectionBuilder<ClientBuilder>
    {
    public:
        ClientBuilder& buildInboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mInboundInterceptors);
            return *this;
        }

        ClientBuilder& buildOutboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mOutboundInterceptors);
            return *this;
        }

        template<typename RpcClientType>
        void    asyncConnect(const RpcClientCallback<RpcClientType>& callback)
        {
            auto inboundInterceptors = mInboundInterceptors;
            auto outboundInterceptors = mOutboundInterceptors;
            auto enterCallback = [=] (const brynet::net::TcpConnection::Ptr& session) {
                OnBinaryRpcClient<RpcClientType>(session,
                    inboundInterceptors,
                    outboundInterceptors,
                    callback);
            };

            auto connectionOptions = wrapper::BaseConnectionBuilder<ClientBuilder>::getConnectionOptions();
            connectionOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(enterCallback));

            wrapper::BaseConnectionBuilder<ClientBuilder>::asyncConnect(
                wrapper::BaseConnectionBuilder<ClientBuilder>::getConnectOptions(), 
                connectionOptions);
        }

    protected:
        void buildInterceptor(InterceptorBuilder builder, std::vector<UnaryServerInterceptor>& result)
        {
            BuildInterceptor buildInterceptor(&result);
            builder(buildInterceptor);
        }

    private:
        std::vector<UnaryServerInterceptor>     mInboundInterceptors;
        std::vector<UnaryServerInterceptor>     mOutboundInterceptors;
    };

} }