#pragma once

#include <folly/futures/Future.h>
#include <gayrpc/core/gayrpc_meta.pb.h>

#include <any>
#include <functional>
#include <optional>
#include <string>

namespace gayrpc::core {

using InterceptorReturnType = folly::Future<std::optional<std::string>>;

using ServiceIDType = decltype(std::declval<RpcMeta>().service_id());
using ServiceFunctionMsgIDType = uint64_t;
using InterceptorContextType = std::map<std::string, std::any>;
using UnaryHandler = std::function<InterceptorReturnType(gayrpc::core::RpcMeta&&,
                                                         const google::protobuf::Message&,
                                                         InterceptorContextType&&)>;
using UnaryServerInterceptor = std::function<InterceptorReturnType(gayrpc::core::RpcMeta&&,
                                                                   const google::protobuf::Message&,
                                                                   UnaryHandler&& next,
                                                                   InterceptorContextType&& context)>;

}// namespace gayrpc::core
