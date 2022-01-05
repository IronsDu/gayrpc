#include <gayrpc/utils/UtilsWrapper.h>

#include <atomic>
#include <brynet/base/AppStatus.hpp>
#include <brynet/net/EventLoop.hpp>
#include <brynet/net/TcpService.hpp>
#include <iostream>

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::benchmark;
using namespace gayrpc::core;
using namespace gayrpc::utils;

static std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        : EchoServerService(std::move(context))
    {}

    void Echo(const EchoRequest& request,
              const EchoReply::Ptr& replyObj,
              InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response, std::move(context));
    }
};

static auto counter(RpcMeta&& meta,
                    const google::protobuf::Message& message,
                    UnaryHandler&& next,
                    InterceptorContextType&& context)
{
    count++;
    return next(std::move(meta), message, std::move(context));
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: <listen port> <thread num>\n");
        exit(-1);
    }

    auto port = std::stoi(argv[1]);
    std::cout << "listen port:" << port << std::endl;

    auto service = TcpService::Create();
    service->startWorkerThread(std::thread::hardware_concurrency());

    auto serviceBuild = ServiceBuilder();
    serviceBuild.buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
                    buildInterceptors.addInterceptor(counter);
                })
            .WithMaxRecvBufferSize(1024 * 1024)
            .WithService(service)
            .addServiceCreator([](gayrpc::core::ServiceContext&& context) {
                return std::make_shared<MyService>(std::move(context));
            })
            .WithAddr(false, "0.0.0.0", port)
            .asyncRun();

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);

    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count - tmp) << std::endl;
        tmp.store(count);
        if (brynet::base::app_kbhit() > 0)
        {
            break;
        }
    }

    return 0;
}
