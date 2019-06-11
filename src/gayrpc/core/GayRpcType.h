#pragma once

#include <functional>
#include <any>
#include <gayrpc/core/gayrpc_meta.pb.h>

namespace gayrpc { namespace core {

    using ServiceIDType = decltype(std::declval<RpcMeta>().service_id());
    using ServiceFunctionMsgIDType = uint64_t;
    using InterceptorContextType = std::map<std::string, std::any>;
    using UnaryHandler = std::function<void(gayrpc::core::RpcMeta&&, const google::protobuf::Message&, InterceptorContextType&&)>;
    using UnaryServerInterceptor = std::function<void(gayrpc::core::RpcMeta&&, const google::protobuf::Message&, UnaryHandler&& next, InterceptorContextType&& context)>;

} }