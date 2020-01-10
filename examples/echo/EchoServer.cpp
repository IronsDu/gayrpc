#include <iostream>
#include <atomic>

#include <brynet/net/EventLoop.hpp>
#include <brynet/net/TcpService.hpp>
#include <brynet/net/ListenThread.hpp>
#include <brynet/base/AppStatus.hpp>
#include <gayrpc/utils/UtilsWrapper.h>
#include "./pb/echo_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace gayrpc::core;
using namespace dodo::test;
using namespace gayrpc::utils;

std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        :
        EchoServerService(std::move(context))
    {
        mClient = EchoServerClient::Create(context.getTypeHandleManager(), 
            context.getInInterceptor(), 
            context.getOutInterceptor());
    }

    void Echo(const EchoRequest& request, 
        const EchoReply::PTR& replyObj,
        InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response, std::move(context));
    }

    void Login(const LoginRequest& request,
        const LoginReply::PTR& replyObj,
        InterceptorContextType&& context) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response, std::move(context));
    }

    void    onClose() override
    {
        std::cout << "closed" << std::endl;
    }

private:
    std::shared_ptr<EchoServerClient>   mClient;
};

static auto counter(RpcMeta&& meta, 
    const google::protobuf::Message& message, 
    UnaryHandler&& next, 
    InterceptorContextType&& context)
{
    count++;
    return next(std::move(meta), message, std::move(context));
}

static auto auth(RpcMeta&& meta,
    const google::protobuf::Message& message,
    UnaryHandler&& next,
    InterceptorContextType&& context)
{
    // auth ¥¶¿Ì
    if (true) {
        return ananas::MakeReadyFuture(std::optional<std::string>("auth failed"));
    }

    return next(std::move(meta), message, std::move(context));
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: <listen port> <thread num>\n");
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(std::atoi(argv[2]));

    auto serviceBuild = ServiceBuilder();
    serviceBuild.buildOutboundInterceptor([](BuildInterceptor buildInterceptors) {
            buildInterceptors.addInterceptor(counter);
            buildInterceptors.addInterceptor(gayrpc::utils::withProtectedCall());
        })
        .buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
            buildInterceptors.addInterceptor(auth);
            buildInterceptors.addInterceptor(gayrpc::utils::withProtectedCall());
        })
        .configureConnectionOptions({
            AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            AddSocketOption::AddEnterCallback([](const TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            })
        })
        .configureTcpService(service)
        .addServiceCreator([](gayrpc::core::ServiceContext&& context) {
            return std::make_shared<MyService>(std::move(context));
        })
        .configureListen([=](wrapper::BuildListenConfig listenConfig) {
            listenConfig.setAddr(false, "0.0.0.0", std::stoi(argv[1]));
        })
        .configureTransportType([](BuildTransportType buildTransportType) {
            buildTransportType.setType(TransportType::Binary);
        })
        .asyncRun();

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);
    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
        if(brynet::base::app_kbhit() > 0)
        {
            break;
        }
    }
}
