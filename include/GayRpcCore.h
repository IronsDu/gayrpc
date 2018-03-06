#ifndef _GAY_RPC_CORE_H
#define _GAY_RPC_CORE_H

#include <functional>

#include "meta.pb.h"

namespace gayrpc
{
    namespace core
    {
        typedef std::function<void(const gayrpc::core::RpcMeta&, const google::protobuf::Message&)> UnaryHandler;
        typedef std::function<void(const gayrpc::core::RpcMeta&, const google::protobuf::Message&, const UnaryHandler& next)> UnaryServerInterceptor;
    }
}

#endif