#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"
#include <vector>
#include <gayrpc/core/GayRpcInterceptor.h>

TEST_CASE("interceptor are computed", "[interceptor]")
{
    // normal
    {
        auto interceptor = gayrpc::core::makeInterceptor();
        gayrpc::core::RpcMeta meta;
        google::protobuf::Message* message = nullptr;

        int v = 0;

        interceptor(
            meta, 
            *message, 
            [&v](const gayrpc::core::RpcMeta&, const google::protobuf::Message&) {
                v++;
            });

        REQUIRE(v == 1);
    }

    // normal
    {
        std::vector<int> vlist;

        auto interceptor = gayrpc::core::makeInterceptor(
            [&vlist](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next){
                vlist.push_back(1);
                next(meta, message);
            },
            [&vlist](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next){
                vlist.push_back(2);
                next(meta, message);
            });

        gayrpc::core::RpcMeta meta;
        google::protobuf::Message* message = nullptr;

        interceptor(
            meta, 
            *message, 
            [&vlist](const gayrpc::core::RpcMeta&, const google::protobuf::Message&) {
                vlist.push_back(3);
            });

        std::vector<int> tmp = {1, 2, 3};
        REQUIRE(vlist == tmp);
    }

    // yield interceptor
    {
        std::vector<int> vlist;

        auto interceptor = gayrpc::core::makeInterceptor(
            [&vlist](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next) {
                vlist.push_back(1);
            },
            [&vlist](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next) {
                vlist.push_back(2);
                next(meta, message);
            });

        gayrpc::core::RpcMeta meta;
        google::protobuf::Message* message = nullptr;

        interceptor(
            meta,
            *message,
            [&vlist](const gayrpc::core::RpcMeta&, const google::protobuf::Message&) {
                vlist.push_back(3);
            });

        std::vector<int> tmp = {1};
        REQUIRE(vlist == tmp);
    }

    // resume interceptor
    {
        std::vector<int> vlist;

        gayrpc::core::UnaryHandler tmpHandler;
        gayrpc::core::RpcMeta meta;
        google::protobuf::Message* message = nullptr;

        {
            auto interceptor = gayrpc::core::makeInterceptor(
                [&](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next) {
                    vlist.push_back(1);
                    tmpHandler = next;
                },
                [&](const gayrpc::core::RpcMeta& meta, const google::protobuf::Message& message, const gayrpc::core::UnaryHandler& next) {
                    vlist.push_back(2);
                    next(meta, message);
                });


            interceptor(
                meta,
                *message,
                [&vlist](const gayrpc::core::RpcMeta&, const google::protobuf::Message&) {
                    vlist.push_back(3);
                });
        }

        REQUIRE(vlist == std::vector<int>{1});
        tmpHandler(meta, *message);
        REQUIRE(vlist == std::vector<int>{1,2,3});
    }
}