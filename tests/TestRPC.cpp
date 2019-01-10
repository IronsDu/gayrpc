#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"
#include <vector>
#include <gayrpc/core/GayRpcInterceptor.h>
#include "./pb/echo_service.gayrpc.h"

const std::string hello("hello");
const std::string world("world");

TEST_CASE("rpc are computed", "[rpc]")
{
    using namespace gayrpc::core;

    class MyService : public dodo::test::EchoServerService
    {
    public:
        MyService(gayrpc::core::ServiceContext context)
            :
            dodo::test::EchoServerService(context)
        {}

        virtual void Echo(const dodo::test::EchoRequest& request,
            const dodo::test::EchoServerService::EchoReply::PTR& replyObj,
            InterceptorContextType context)
        {
            receivedString = request.message();
            dodo::test::EchoResponse response;
            response.set_message(world);
            replyObj->reply(response, std::move(context));
        }

        virtual void Login(const dodo::test::LoginRequest& request,
            const dodo::test::EchoServerService::LoginReply::PTR& replyObj,
            InterceptorContextType context)
        {

        }

    public:
        std::string receivedString;
    };


    gayrpc::core::RpcMeta responseMeta;
    std::string responseBody;

    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();

    gayrpc::core::RpcMeta requestMeta;
    std::string requestBody;

    auto client = dodo::test::EchoServerClient::Create(rpcHandlerManager,
        [&](const gayrpc::core::RpcMeta& meta,
        const google::protobuf::Message& message,
        const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            return next(meta, message, std::move(context));
        },
        [&](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            requestMeta = meta;
            requestBody = message.SerializeAsString();
            return next(meta, message, std::move(context));
        });

    dodo::test::EchoRequest request;
    request.set_message(hello);

    std::string expectedResponse;
    client->Echo(request,
        [&](const dodo::test::EchoResponse& response, const gayrpc::core::RpcError&) {
            expectedResponse = response.message();
        });

    gayrpc::core::ServiceContext serviceContext(rpcHandlerManager,
        [&](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            return next(meta, message, std::move(context));
        },
        [&](const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            InterceptorContextType context)
        {
            responseMeta = meta;
            responseBody = message.SerializeAsString();
            return next(meta, message, std::move(context));
        });
    auto service = std::make_shared<MyService>(serviceContext);
    dodo::test::EchoServerService::Install(service);

    InterceptorContextType context;
    rpcHandlerManager->handleRpcMsg(requestMeta, requestBody, std::move(context));
    REQUIRE(service->receivedString == hello);

    rpcHandlerManager->handleRpcMsg(responseMeta, responseBody, std::move(context));
    REQUIRE(expectedResponse == world);

    return;
}