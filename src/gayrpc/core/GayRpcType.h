#pragma once

#include <optional>
#include <string>
#include <functional>
#include <any>
#include <gayrpc/core/gayrpc_meta.pb.h>
#include <ananas/future/Future.h>

namespace gayrpc { namespace core {

    using InterceptorReturnType = ananas::Future<std::optional<std::string>>;

    using ServiceIDType = decltype(std::declval<RpcMeta>().service_id());
    using ServiceFunctionMsgIDType = uint64_t;
    using InterceptorContextType = std::map<std::string, std::any>;
    using UnaryHandler = std::function<InterceptorReturnType(gayrpc::core::RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&&)>;
    using UnaryServerInterceptor = std::function<InterceptorReturnType(gayrpc::core::RpcMeta&&,
        const google::protobuf::Message&, 
        UnaryHandler&& next, 
        InterceptorContextType&& context)>;

} }