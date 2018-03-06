#ifndef _GAY_RPC_SERVICE_H
#define _GAY_RPC_SERVICE_H

#include <memory>

namespace gayrpc
{
    namespace core
    {
        class BaseService : public std::enable_shared_from_this<BaseService>
        {
        };
    }
}

#endif