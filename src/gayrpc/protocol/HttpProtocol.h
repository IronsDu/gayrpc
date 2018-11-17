#pragma once

#include <string>

#include <google/protobuf/util/json_util.h>
#include <brynet/net/http/HttpService.h>
#include <brynet/net/http/HttpFormat.h>
#include <gayrpc/core/meta.pb.h>
#include <gayrpc/core/GayRpcTypeHandler.h>

namespace gayrpc { namespace protocol {

    using namespace gayrpc::core;
    using namespace brynet::utils;

    class http
    {
    public:
        static void handleHttpPacket(const gayrpc::core::RpcTypeHandleManager::PTR& rpcHandlerManager,
            const brynet::net::http::HTTPParser &httpParser,
            const brynet::net::http::HttpSession::PTR &session,
            brynet::net::EventLoop::PTR handleRpcEventLoop)
        {
            (void)session;
            RpcMeta meta;
            const auto &path = httpParser.getPath();
            meta.mutable_request_info()->set_strmethod(path.substr(1, path.size() - 1));
            meta.mutable_request_info()->set_expect_response(true);
            meta.set_encoding(RpcMeta::JSON);

            if (handleRpcEventLoop != nullptr) {
                handleRpcEventLoop->pushAsyncProc([=, meta = std::move(meta)]() {
                    rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
                });
            }
            else {
                rpcHandlerManager->handleRpcMsg(meta, httpParser.getBody());
            }
        }

        static void send(const gayrpc::core::RpcMeta& meta,
            const google::protobuf::Message& message,
            const gayrpc::core::UnaryHandler& next,
            const brynet::net::http::HttpSession::PTR& httpSession)
        {
            std::string jsonMsg;
            google::protobuf::util::MessageToJsonString(message, &jsonMsg);

            brynet::net::http::HttpResponse httpResponse;
            httpResponse.setStatus(brynet::net::http::HttpResponse::HTTP_RESPONSE_STATUS::OK);
            httpResponse.setContentType("application/json");
            httpResponse.setBody(jsonMsg.c_str());

            auto result = httpResponse.getResult();
            httpSession->send(result.c_str(), result.size(), nullptr);

            httpSession->postShutdown();

            next(meta, message);
        }
    };
    
} }
