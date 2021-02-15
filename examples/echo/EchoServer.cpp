#include <gayrpc/utils/UtilsWrapper.h>

#include <atomic>
#include <bsio/Bsio.hpp>
#include <iostream>

#include "./pb/echo_service.gayrpc.h"

using namespace asio;
using namespace asio::ip;
using namespace bsio::net;
using namespace gayrpc::core;
using namespace dodo::test;
using namespace gayrpc::utils;

std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        : EchoServerService(std::move(context))
    {
        mClient = EchoServerClient::Create(context.getTypeHandleManager(),
                                           context.getInInterceptor(),
                                           context.getOutInterceptor());
    }

    void Echo(const EchoRequest& request,
              const EchoReply::Ptr& replyObj,
              InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message("world");

        replyObj->reply(response, std::move(context));
    }

    void Login(const LoginRequest& request,
               const LoginReply::Ptr& replyObj,
               InterceptorContextType&& context) override
    {
        LoginResponse response;
        response.set_message(request.message());
        replyObj->reply(response, std::move(context));
    }

    void onClose() override
    {
        std::cout << "closed" << std::endl;
    }

private:
    std::shared_ptr<EchoServerClient> mClient;
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
    if (true)
    {
        //return ananas::MakeReadyFuture(std::optional<std::string>("auth failed"));
    }

    return next(std::move(meta), message, std::move(context));
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: <listen port> <thread num>\n");
        exit(-1);
    }

    auto ioContextThreadPool = IoContextThreadPool::Make(std::atoi(argv[2]), 1);
    ioContextThreadPool->start(1);

    IoContextThread listenContextWrapper(1);
    listenContextWrapper.start(1);

    TcpAcceptor::Ptr acceptor = TcpAcceptor::Make(
            listenContextWrapper.context(),
            ioContextThreadPool,
            ip::tcp::endpoint(ip::tcp::v4(), std::atoi(argv[1])));

    auto serviceBuild = ServiceBuilder();
    serviceBuild.buildOutboundInterceptor([](BuildInterceptor buildInterceptors) {
                    buildInterceptors.addInterceptor(counter);
                    buildInterceptors.addInterceptor(gayrpc::utils::withProtectedCall());
                })
            .buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
                buildInterceptors.addInterceptor(auth);
                buildInterceptors.addInterceptor(gayrpc::utils::withProtectedCall());
            })
            .WithAcceptor(acceptor)
            .WithRecvBufferSize(1024 * 1024)
            .addServiceCreator([](gayrpc::core::ServiceContext&& context) {
                return std::make_shared<MyService>(std::move(context));
            })
            .configureTransportType([](BuildTransportType buildTransportType) {
                buildTransportType.setType(TransportType::Binary);
            })
            .asyncRun();

    WrapperIoContext mainLoop(1);

    asio::signal_set sig(mainLoop.context(), SIGINT, SIGTERM);
    sig.async_wait([&](const asio::error_code& err, int signal) {
        mainLoop.stop();
    });
    std::atomic<int64_t> tmp(0);

    for (; !mainLoop.context().stopped();)
    {
        mainLoop.context().run_one_for(std::chrono::seconds(1));
        std::cout << "count :" << (count - tmp) << std::endl;
        tmp.store(count);
    }
}
