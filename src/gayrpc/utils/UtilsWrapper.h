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

    template<typename RpcClientType>
    using RpcClientCallback = std::function<void(std::shared_ptr<RpcClientType>)>;

    class ListenConfig
    {
    public:
        ListenConfig()
        {
            mIsIpV6 = false;
        }

        void        setAddr(bool ipV6, std::string ip, int port)
        {
            mIsIpV6 = ipV6;
            mListenAddr = ip;
            mPort = port;
        }

        std::string ip() const
        {
            return mListenAddr;
        }

        int         port() const
        {
            return mPort;
        }

        bool        useIpV6() const
        {
            return mIsIpV6;
        }

    private:
        std::string mListenAddr;
        int         mPort;
        bool        mIsIpV6;
    };


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

        ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);

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

        ServiceContext serviceContext(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        auto service = serverCreator(serviceContext);
        RpcServiceType::Install(service);
    }

    class BuildInterceptor
    {
    public:
        BuildInterceptor(std::vector< UnaryServerInterceptor>* nterceptors)
        {
            mInterceptors = nterceptors;
        }

        void    addInterceptor(UnaryServerInterceptor interceptor)
        {
            mInterceptors->push_back(interceptor);
        }

    private:
        std::vector< UnaryServerInterceptor>*    mInterceptors;
    };

    class BuildSocketOptions
    {
    public:
        BuildSocketOptions(std::vector<TcpService::AddSocketOption::AddSocketOptionFunc>* options)
        {
            mOptions = options;
        }

        void    addOption(TcpService::AddSocketOption::AddSocketOptionFunc option)
        {
            mOptions->push_back(option);
        }
    private:
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc>* mOptions;
    };

    class BuildConnectOptions
    {
    public:
        BuildConnectOptions(std::vector<AsyncConnector::ConnectOptions::ConnectOptionFunc>* options)
        {
            mOptions = options;
        }

        void    addOption(AsyncConnector::ConnectOptions::ConnectOptionFunc option)
        {
            mOptions->push_back(option);
        }
    private:
        std::vector < AsyncConnector::ConnectOptions::ConnectOptionFunc>* mOptions;
    };

    class BuildListenConfig
    {
    public:
        BuildListenConfig(ListenConfig* config)
        {
            mConfig = config;
        }

        void        setAddr(bool ipV6, std::string ip, int port)
        {
            mConfig->setAddr(ipV6, ip, port);
        }
    private:
        ListenConfig* mConfig;
    };

    enum class TransportType
    {
        Binary,
        HTTP,
    };
    class TransportTypeConfig
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

    class BuildTransportType
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
    using SocketOptionsSet = std::function<void(BuildSocketOptions)>;
    using ConnectOptionSet = std::function<void(BuildConnectOptions)>;

    template<typename RpcServiceType>
    class ServiceBuilder : public std::enable_shared_from_this<ServiceBuilder<RpcServiceType>>
    {
    public:
        using Ptr = std::shared_ptr<ServiceBuilder<RpcServiceType>>;
        using ListenOptionsSet = std::function<void(BuildListenConfig)>;
        using BuildTransportTypeSet = std::function<void(BuildTransportType)>;

        static Ptr Make()
        {
            struct make_shared_enabler : public ServiceBuilder<RpcServiceType>
            {
                make_shared_enabler()
                    :
                    ServiceBuilder()
                {}
            };

            return std::make_shared<make_shared_enabler>();
        }

        virtual ~ServiceBuilder()
        {
            stop();
        }

        auto buildInboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mInboundInterceptors);
            return this;
        }

        auto buildOutboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mOutboundInterceptors);
            return this;
        }

        auto buildSocketOptions(SocketOptionsSet builder)
        {
            BuildSocketOptions buildSocketOption(&mSocketOptions);
            builder(buildSocketOption);
            return this;
        }

        auto configureService(brynet::net::TcpService::Ptr service)
        {
            mService = service;
            return this;
        }

        auto configureCreator(ServiceCreator<RpcServiceType> creator)
        {
            mCreator = creator;
            return this;
        }

        auto configureListen(ListenOptionsSet builder)
        {
            BuildListenConfig buildConfig(&mListenConfig);
            builder(buildConfig);
            return this;
        }

        auto    configureTransportType(BuildTransportTypeSet builder)
        {
            BuildTransportType buildTransportType(&mTransportTypeConfig);
            builder(buildTransportType);
            return this;
        }

        void    asyncRun()
        {
            if (mService == nullptr)
            {
                throw std::runtime_error("service is null");
            }

            mSocketOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(
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
                        throw std::runtime_error(std::string("not support transport type:") + std::to_string(static_cast<int>(transportType)));
                        break;
                    }
                }));

            mListenThread = ListenThread::Create(mListenConfig.useIpV6(), 
                mListenConfig.ip(), 
                mListenConfig.port(), 
                [service = mService,
                socketOptions = mSocketOptions](brynet::net::TcpSocket::Ptr socket) {
                    service->addTcpConnection(std::move(socket), socketOptions);
                });
            mListenThread->startListen();
        }

        void    stop()
        {
            if (mListenThread)
            {
                mListenThread->stopListen();
            }
        }

    protected:
        ServiceBuilder()
            :
            mTransportTypeConfig(TransportType::Binary)
        {}

    private:
        void buildInterceptor(InterceptorBuilder builder, std::vector< UnaryServerInterceptor>& result)
        {
            BuildInterceptor buildInterceptor(&result);
            builder(buildInterceptor);
        }

    private:
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc>   mSocketOptions;
        std::vector< UnaryServerInterceptor>    mInboundInterceptors;
        std::vector< UnaryServerInterceptor>    mOutboundInterceptors;
        brynet::net::TcpService::Ptr            mService;
        ServiceCreator<RpcServiceType>          mCreator;
        ListenConfig                            mListenConfig;
        ListenThread::Ptr                       mListenThread;
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
        auto client = RpcClientType::Create(rpcHandlerManager, inboundInterceptor, outBoundInterceptor);
        callback(client);
    }

    class ClientBuilder : public std::enable_shared_from_this<ClientBuilder>
    {
    public:
        using Ptr = std::shared_ptr<ClientBuilder>;

        static Ptr Make()
        {
            struct make_shared_enabler : public ClientBuilder
            {
                make_shared_enabler()
                    :
                    ClientBuilder()
                {}
            };

            return std::make_shared<make_shared_enabler>();
        }

        auto buildInboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mInboundInterceptors);
            return this;
        }

        auto buildOutboundInterceptor(InterceptorBuilder builder)
        {
            buildInterceptor(builder, mOutboundInterceptors);
            return this;
        }

        auto buildSocketOptions(SocketOptionsSet builder)
        {
            BuildSocketOptions buildSocketOption(&mSocketOptions);
            builder(buildSocketOption);
            return this;
        }

        auto buildConnectOptions(ConnectOptionSet builder)
        {
            mConnectOptions.clear();
            BuildConnectOptions buildConnectOption(&mConnectOptions);
            builder(buildConnectOption);
            return this;
        }

        auto configureService(brynet::net::TcpService::Ptr service)
        {
            mService = service;
            return this;
        }

        auto configureConnector(brynet::net::AsyncConnector::Ptr connector)
        {
            mConnector = connector;
            return this;
        }

        template<typename RpcClientType>
        void    asyncConnect(const RpcClientCallback<RpcClientType>& callback)
        {
            if (mService == nullptr)
            {
                throw std::runtime_error("service is null");
            }
            if (mConnector == nullptr)
            {
                throw std::runtime_error("connector is null");
            }

            auto enterCallback = [
                inboundInterceptors = mInboundInterceptors,
                outboundInterceptors = mOutboundInterceptors,
                socketOptions = mSocketOptions,
                callback,
                service = mService]
                (brynet::net::TcpSocket::Ptr socket) mutable {

                auto enterCallback = [=]
                    (const brynet::net::TcpConnection::Ptr &session) {
                    OnBinaryRpcClient<RpcClientType>(session, 
                        inboundInterceptors, 
                        outboundInterceptors, 
                        callback);
                };

                socket->setNodelay();
                socketOptions.push_back(TcpService::AddSocketOption::AddEnterCallback(enterCallback));
                service->addTcpConnection(std::move(socket), socketOptions);
            };
            mConnectOptions.push_back(AsyncConnector::ConnectOptions::WithCompletedCallback(enterCallback));
            mConnector->asyncConnect(mConnectOptions);
        }

    protected:
        ClientBuilder() = default;
        virtual ~ClientBuilder() = default;

        void buildInterceptor(InterceptorBuilder builder, std::vector< UnaryServerInterceptor>& result)
        {
            BuildInterceptor buildInterceptor(&result);
            builder(buildInterceptor);
        }

    private:
        brynet::net::TcpService::Ptr            mService;
        brynet::net::AsyncConnector::Ptr        mConnector;
        std::vector< UnaryServerInterceptor>    mInboundInterceptors;
        std::vector< UnaryServerInterceptor>    mOutboundInterceptors;
        std::vector<TcpService::AddSocketOption::AddSocketOptionFunc>   mSocketOptions;
        std::vector<AsyncConnector::ConnectOptions::ConnectOptionFunc>  mConnectOptions;
    };

} }