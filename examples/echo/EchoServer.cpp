#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <gayrpc/utils/UtilsWrapper.h>

#include <atomic>
#include <brynet/base/AppStatus.hpp>
#include <brynet/net/EventLoop.hpp>
#include <brynet/net/TcpService.hpp>
#include <format>
#include <iostream>
#include <thread>

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

folly::coro::Mutex mutex;

folly::coro::Task<void> asyncJob()
{
    folly::Promise<int> promise;
    std::thread([&]() {
        std::cout << "in thread, id:[" << std::this_thread::get_id() << "]" << std::endl;
        promise.setValue(100);
        std::cout << "end in thread, id:[" << std::this_thread::get_id() << "]" << std::endl;
    }).detach();

    std::cout << "start wait async result, current thead id:" << std::this_thread::get_id() << std::endl;
    co_await promise.getFuture();
    std::cout << "start sleep, current thead id:" << std::this_thread::get_id() << std::endl;
    co_await folly::futures::sleep(std::chrono::seconds{5});
    std::cout << "end sleep" << std::endl;
}


folly::coro::Task<int> oneTask()
{
    folly::Promise<int> promise;
    std::thread([&]() {
        promise.setValue(100);
    }).detach();
    ;
    co_return co_await promise.getFuture();
}

folly::coro::Task<int> task43()
{
    folly::Executor* startExecutor = co_await folly::coro::co_current_executor;
    asyncJob().scheduleOn(startExecutor).start();

    auto value = co_await oneTask();
    auto lock = co_await mutex.co_scoped_lock();
    std::cout << "value1:" << value << std::endl;
    co_return value + 1;
}

int gv = 0;
folly::coro::Mutex gm;
folly::coro::Task<void> taskCalc()
{
    auto lock = co_await mutex.co_scoped_lock();
    gv++;
}


int main(int argc, char** argv)
{
    {
        std::cout << "main id:" << std::this_thread::get_id() << std::endl;
        folly::getGlobalCPUExecutor()->add([]() {
            std::cout << "add id:" << std::this_thread::get_id() << std::endl;
            std::cout << "add:" << 123 << std::endl;
        });
        task43().scheduleOn(folly::getGlobalCPUExecutor().get()).start();
        auto t2 = folly::coro::co_invoke([]() -> folly::coro::Task<void> {
            auto value = co_await oneTask();
            auto lock = co_await mutex.co_scoped_lock();
            std::cout << "value2:" << value << std::endl;
        });
        std::move(t2).scheduleOn(folly::getGlobalCPUExecutor().get()).start();

        for (int i = 0; i < 2000000; i++)
        {
            taskCalc().scheduleOn(folly::getGlobalCPUExecutor().get()).start();
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "Hello World!\n";
        std::cout << "gv:" << gv << std::endl;
    }

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
            .WithMaxRecvBufferSize(1024 * 1024)
            .WithService(service)
            .addServiceCreator([](gayrpc::core::ServiceContext&& context) {
                return std::make_shared<MyService>(std::move(context));
            })
            .WithAddr(false, "0.0.0.0", std::stoi(argv[1]))
            .configureTransportType([](BuildTransportType buildTransportType) {
                buildTransportType.setType(TransportType::Binary);
            })
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
}
