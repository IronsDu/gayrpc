#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"
#include <vector>
#include <any>
#include <gayrpc/core/GayRpcInterceptor.h>
#include <brynet/net/Connector.h>
#include <brynet/net/TCPService.h>
#include <gayrpc/utils/UtilsWrapper.h>
#include <brynet/net/TcpConnection.h>

#include "./pb/echo_service.gayrpc.h"
#include "./pb/orleans_service.gayrpc.h"

const std::string OrleansReplyObjKey = "reply";
const std::string EchoServiceGrainTypeName("echo_service");
const std::string ServiceIP("127.0.0.1");
const int ServicePort = 8888;

const std::string hello("hello");
const std::string world("world");

const brynet::net::EventLoop::Ptr mainLoop = std::make_shared<brynet::net::EventLoop>();
const brynet::net::TcpService::Ptr tcpService = brynet::net::TcpService::Create();
const brynet::net::AsyncConnector::Ptr connector = brynet::net::AsyncConnector::Create();

using OrleanAddr = std::pair<std::string, int>;
std::map<OrleanAddr, dodo::test::OrleansServiceClient::PTR> orleans;
std::map<std::string, gayrpc::core::RpcTypeHandleManager::PTR> handlers;
std::map<std::string, gayrpc::core::RpcTypeHandleManager::PTR> grains;

using GrainTypeName = std::string;
std::map <GrainTypeName, std::function<gayrpc::core::RpcTypeHandleManager::PTR (std::string)>> grainCreator;

using namespace brynet::net;
using namespace gayrpc::core;
using namespace gayrpc::utils;

// �ڵ�ͨ�ŷ���
class MyOrleansGrainService : public dodo::test::OrleansServiceService
{
public:
    MyOrleansGrainService(gayrpc::core::ServiceContext context)
        :
        dodo::test::OrleansServiceService(context)
    {}

    virtual void Request(const dodo::test::OrleansRequest& request,
        const dodo::test::OrleansServiceService::RequestReply::PTR& replyObj,
        InterceptorContextType context)
    {
        gayrpc::core::RpcTypeHandleManager::PTR rpcHandle;
        auto grain = grains.find(request.grain_name());
        if (grain == grains.end())
        {
            auto creator = grainCreator.find(request.grain_type());
            if (creator == grainCreator.end())
            {
                //TODO:: error
                return;
            }
            rpcHandle = creator->second(request.grain_name());
        }
        context[OrleansReplyObjKey] = replyObj;
        rpcHandle->handleRpcMsg(request.meta(), request.body(), std::move(context));
    }
};

dodo::test::OrleansServiceClient::PTR findOrAsyncCreateOrleanConnection(OrleanAddr addr)
{
    auto it = orleans.find(addr);
    if (it == orleans.end())
    {
        return nullptr;
    }

    return (*it).second;
}

using OrleansCreatedCallback = std::function<void(dodo::test::OrleansServiceClient::PTR)>;
void OrleansConnectionCreatedCallback(OrleanAddr addr, OrleansCreatedCallback callback)
{
    auto orleans = findOrAsyncCreateOrleanConnection(addr);
    if (orleans == nullptr)
    {
        // �����ǰû�е��ڵ���������첽����
        AsyncCreateRpcClient<dodo::test::OrleansServiceClient>(
            tcpService,
            connector,
            {
                    AsyncConnector::ConnectOptions::WithAddr(addr.first, addr.second),
                    AsyncConnector::ConnectOptions::WithTimeout(std::chrono::seconds(10)),
                    AsyncConnector::ConnectOptions::WithFailedCallback([]() {
                        std::cout << "failed" << std::endl;
                    }),
            },
            {
                TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
                TcpService::AddSocketOption::AddEnterCallback([](const TcpConnection::Ptr& session) {
                    session->setHeartBeat(std::chrono::seconds(10));
                })
            },
            {},
            [=](dodo::test::OrleansServiceClient::PTR client) {
                // RPC���󴴽��ɹ���ִ�лص�
                callback(client);
            });
    }
    else
    {
        // ֱ��ִ�лص�
        callback(orleans);
    }
}

template<typename T>
auto TakeGrain(std::string name)
{   
    auto grainRpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    auto grain = T::Create(grainRpcHandlerManager,
        [=](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            return next(meta, message, std::move(context));
        },
        [=](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            // outboundInterceptor
            // ����ҵ���RPC Client�����(��Request)
            // ��ҵ��RPC������ OrleansRequest
            dodo::test::OrleansRequest request;
            //TODO:: EchoServiceGrainTypeName -> generate typename
            request.set_grain_type(EchoServiceGrainTypeName);
            request.set_grain_name(name);
            *request.mutable_meta() = meta;
            request.set_body(message.SerializeAsString());

            std::shared_ptr<google::protobuf::Message> p(message.New());
            p->CopyFrom(message);

            // ���Դ�����grain���ڽڵ��RPC
            OrleansConnectionCreatedCallback(std::make_pair(ServiceIP, ServicePort), [=, context = std::move(context)](dodo::test::OrleansServiceClient::PTR orleanClient) {
                orleanClient->Request(request, [=, context = std::move(context)](const dodo::test::OrleansResponse& response, const gayrpc::core::RpcError&) {
                    // ���յ���response�����û���RPC
                    InterceptorContextType context;
                    grainRpcHandlerManager->handleRpcMsg(response.meta(), response.body(), context);
                });
                next(meta, *p, std::move(context));
            });
        });

    return grain;
}

template<typename T>
void RegisterGrainCreator(GrainTypeName typeName)
{
    grainCreator[typeName] = [](std::string grainName) {
        // ����Grain ����
        auto grainRpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
        gayrpc::core::ServiceContext serviceContext(grainRpcHandlerManager,
            [=](const gayrpc::core::RpcMeta& meta,
                const google::protobuf::Message& message,
                const gayrpc::core::UnaryHandler& next,
                InterceptorContextType context)
            {
                return next(meta, message, std::move(context));
            },
            [=](const gayrpc::core::RpcMeta& meta,
                const google::protobuf::Message& message,
                const gayrpc::core::UnaryHandler& next,
                InterceptorContextType context)
            {
                // ����ҵ���RPC��������(��Response)

                auto replyObj = context[OrleansReplyObjKey];
                auto replyObjPtr = std::any_cast<dodo::test::OrleansServiceService::RequestReply::PTR>(replyObj);
                assert(replyObjPtr != nullptr);
                // �õײ�RPC��װҵ����RPC Response
                dodo::test::OrleansResponse response;
                *response.mutable_meta() = meta;
                response.set_body(message.SerializeAsString());
                replyObjPtr->reply(response, std::move(context));

                return next(meta, message, std::move(context));
            });
        auto service = std::make_shared<T>(serviceContext);
        T::Install(service);

        grains[grainName] = grainRpcHandlerManager;

        return grainRpcHandlerManager;
    };
}

// ҵ��Grain����
class MyEchoService : public dodo::test::EchoServerService
{
public:
    MyEchoService(gayrpc::core::ServiceContext context)
        :
        dodo::test::EchoServerService(context)
    {}
    virtual void Echo(const dodo::test::EchoRequest& request,
        const dodo::test::EchoServerService::EchoReply::PTR& replyObj,
        InterceptorContextType context)
    {
        REQUIRE(request.message() == hello);
        dodo::test::EchoResponse response;
        response.set_message(world);
        replyObj->reply(response, std::move(context));
    }
    virtual void Login(const dodo::test::LoginRequest& request,
        const dodo::test::EchoServerService::LoginReply::PTR& replyObj,
        InterceptorContextType context)
    {
    }
};

TEST_CASE("orleans are computed", "[orleans]")
{
    tcpService->startWorkerThread(1);
    connector->startWorkerThread();

    auto config = gayrpc::utils::WrapTcpRpc<MyOrleansGrainService>(tcpService,
        [](gayrpc::core::ServiceContext context) {
            return std::make_shared<MyOrleansGrainService>(context);
        }, 
        {
            TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            TcpService::AddSocketOption::AddEnterCallback([](const brynet::net::TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            }),
        },
        {});
    auto binaryListenThread = brynet::net::ListenThread::Create(false, ServiceIP, ServicePort, config);
    binaryListenThread->startListen();

    RegisterGrainCreator<MyEchoService>(EchoServiceGrainTypeName);

    auto echoServer1Grain = TakeGrain<dodo::test::EchoServerClient>("echo_server_1");
    dodo::test::EchoRequest request;
    request.set_message(hello);

    auto waitPromise = std::make_shared<std::promise<std::string>>();

    echoServer1Grain->Echo(request, [=](dodo::test::EchoResponse response, gayrpc::core::RpcError error) {
        waitPromise->set_value(response.message());
    });

    REQUIRE(waitPromise->get_future().get() == world);
    return;
}