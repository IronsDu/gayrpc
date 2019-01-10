#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"
#include <vector>
#include <any>
#include <gayrpc/core/GayRpcInterceptor.h>
#include <brynet/net/Connector.h>
#include <brynet/net/TCPService.h>
#include <gayrpc/utils/UtilsWrapper.h>

#include "./pb/echo_service.gayrpc.h"
#include "./pb/orleans_service.gayrpc.h"

const std::string OrleansReplyObjKey = "reply";
const std::string EchoServiceGrainTypeName("echo_service");
const std::string ServiceIP("127.0.0.1");
const int ServicePort = 9999;

const std::string hello("hello");
const std::string world("world");

const brynet::net::EventLoop::PTR mainLoop = std::make_shared<brynet::net::EventLoop>();
const brynet::net::TcpService::PTR tcpService = brynet::net::TcpService::Create();
const brynet::net::AsyncConnector::PTR connector = brynet::net::AsyncConnector::Create();

using OrleanAddr = std::pair<std::string, int>;
std::map<OrleanAddr, dodo::test::OrleansServiceClient::PTR> orleans;
std::map<std::string, gayrpc::core::RpcTypeHandleManager::PTR> handlers;
std::map<std::string, gayrpc::core::RpcTypeHandleManager::PTR> grains;

using GrainTypeName = std::string;
std::map <GrainTypeName, std::function<gayrpc::core::RpcTypeHandleManager::PTR (std::string)>> grainCreator;

using namespace gayrpc::core;

// 节点通信服务
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
        // 如果当前没有到节点的链接则异步创建
        gayrpc::utils::AsyncCreateRpcClient<dodo::test::OrleansServiceClient>(
            tcpService,
            connector,
            addr.first, addr.second, std::chrono::seconds(10),
            nullptr, nullptr,
            nullptr, [=](dodo::test::OrleansServiceClient::PTR client) {
                // RPC对象创建成功则执行回调
                callback(client);
            }, []() {}, 1024 * 1024, std::chrono::seconds(10));
    }
    else
    {
        // 直接执行回调
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
            // 处理业务层RPC Client的输出(即Request)
            // 将业务RPC包裹在 OrleansRequest
            dodo::test::OrleansRequest request;
            //TODO:: EchoServiceGrainTypeName -> generate typename
            request.set_grain_type(EchoServiceGrainTypeName);
            request.set_grain_name(name);
            *request.mutable_meta() = meta;
            request.set_body(message.SerializeAsString());

            std::shared_ptr<google::protobuf::Message> p(message.New());
            p->CopyFrom(message);

            // 尝试创建到grain所在节点的RPC
            OrleansConnectionCreatedCallback(std::make_pair(ServiceIP, ServicePort), [=, context = std::move(context)](dodo::test::OrleansServiceClient::PTR orleanClient) {
                orleanClient->Request(request, [=, context = std::move(context)](const dodo::test::OrleansResponse& response, const gayrpc::core::RpcError&) {
                    // 将收到的response交给用户层RPC
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
        // 创建Grain 服务
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
                // 处理业务层RPC服务的输出(即Response)

                auto replyObj = context[OrleansReplyObjKey];
                auto replyObjPtr = std::any_cast<dodo::test::OrleansServiceService::RequestReply::PTR>(replyObj);
                assert(replyObjPtr != nullptr);
                // 用底层RPC包装业务层的RPC Response
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

// 业务Grain服务
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

    auto binaryListenThread = brynet::net::ListenThread::Create();
    gayrpc::utils::StartBinaryRpcServer<MyOrleansGrainService>(tcpService, binaryListenThread,
        ServiceIP, ServicePort, [](gayrpc::core::ServiceContext context) {
            return std::make_shared<MyOrleansGrainService>(context);
        }, nullptr, nullptr, nullptr, 1024 * 1024, std::chrono::seconds(10));

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