#pragma once

#include <gayrpc/core/GayRpcTypeHandler.h>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <google/protobuf/util/json_util.h>

#include <bsio/net/http/HttpFormat.hpp>
#include <bsio/net/http/HttpService.hpp>
#include <string>

namespace gayrpc::protocol {

using namespace gayrpc::core;
using namespace bsio::base;

class http
{
public:
    static void handleHttpPacket(const gayrpc::core::RpcTypeHandleManager::Ptr& rpcHandlerManager,
                                 const bsio::net::http::HTTPParser& httpParser,
                                 const bsio::net::http::HttpSession::Ptr& session)
    {
        (void) session;
        RpcMeta meta;
        const auto& path = httpParser.getPath();
        meta.mutable_request_info()->set_strmethod(path.substr(1, path.size() - 1));
        meta.mutable_request_info()->set_expect_response(true);
        meta.set_encoding(RpcMeta::JSON);

        InterceptorContextType context;
        rpcHandlerManager->handleRpcMsg(std::move(meta), httpParser.getBody(), std::move(context));
    }

    static void send(const gayrpc::core::RpcMeta& meta,
                     const google::protobuf::Message& message,
                     const bsio::net::http::HttpSession::Ptr& httpSession)
    {
        std::string jsonMsg;
        google::protobuf::util::MessageToJsonString(message, &jsonMsg);

        bsio::net::http::HttpResponse httpResponse;
        httpResponse.setStatus(bsio::net::http::HttpResponse::HTTP_RESPONSE_STATUS::OK);
        httpResponse.setContentType("application/json");
        httpResponse.setBody(jsonMsg);

        httpSession->send(httpResponse.getResult(), nullptr);
    }
};

}// namespace gayrpc::protocol
