#pragma once

#include <functional>

#include <gayrpc/core/gayrpc_meta.pb.h>

namespace gayrpc { namespace core {

    using ServiceIDType = decltype(std::declval<RpcMeta>().service_id());
    using ServiceFunctionMsgIDType = uint64_t;
    using UnaryHandler = std::function<void(const gayrpc::core::RpcMeta&, const google::protobuf::Message&)>;
    using UnaryServerInterceptor = std::function<void(const gayrpc::core::RpcMeta&, const google::protobuf::Message&, const UnaryHandler& next)>;

} }