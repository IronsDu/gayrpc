
# gayrpc
跨平台全双工双向(异步)RPC系统,也即通信两端都可以同时作为RPC服务方和客户端.

[![Platform](https://img.shields.io/badge/Platform-Linux,%20Windows,%20MacOS-green.svg?style=flat-square)](https://github.com/IronsDu/gayrpc)

## Build Status
[![hosted-ninja-vcpkg-autocache](https://github.com/IronsDu/gayrpc/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/IronsDu/gayrpc/actions/workflows/build_and_test.yml)

## 动机
目前的RPC系统大多用于互联网行业后端系统，但他们之间更像一个单向图(不存在两个服务彼此依赖/互相调用)，但游戏等行业中则两节点之间可能相互调用。
因此我们需要一个全双工RPC，在一个"链接"的两端均可开启服务和客户端，当然这里的"链接"是一个虚拟概念，它不一定基于TCP，也即"链接"的两端可以只存在逻辑链接而没有网络直连。

## 设计准则
1. RPC支持拦截器，能够对Request或Response做一些处理(比如监控、认证、加解密、分布式跟踪)
2. RPC核心不依赖网络和网络传输协议，即：我们可以开发任何网络应用和逻辑来开启RPC两端，将"收到"的消息丢给RPC核心，并通过某个出站拦截器来实现/决定把Request或Response以何种方式传递给谁。
3. 此RPC是基于异步回调的，我认为这是目前C++里比较安全和靠谱的方式，除了回调地狱让人恶心……
4. RPC系统核心（以及接口）是线程安全的，可以在任意线程调用RPC；且可以在任意线程使用XXXReply::PTR对象返回Response。
5. RPC是并行的，也即：客户端可以随意发送Request而不必等待之前的完成。 且允许先收到后发出的Request的Response。
6. RPC系统会为每一个"链接"生成一个XXXService对象，这样可以让不同的"链接"绑定/持有各自的业务对象(有状态）（而不是像grpc等系统那样，一个服务只存在一个service对象，这类RPC调用类似短链接：收到请求返回数据即可）
7. 支持HTTP API(同理2,此功能通过具体通信协议和拦截器进行支持,RPC核心本身与此无关).

## 依赖
Windows下可使用 [vcpkg](https://github.com/Microsoft/vcpkg) 进行安装以下依赖库.

* [protobuf](https://github.com/google/protobuf)
* [brynet](https://github.com/IronsDu/brynet)

请注意,当使用Windows时,务必使用`vcpkg install brynet --head`安装brynet.</br>
且务必根据自身系统中的protoc版本对gayrpc_meta.proto和gayrpc_option.proto预先生成代码，请在 src目录里执行: 
```sh
 protoc --cpp_out=. ./gayrpc/core/gayrpc_meta.proto ./gayrpc/core/gayrpc_option.proto
```

## 代码生成工具
地址：`https://github.com/IronsDu/protoc-gen-gayrpc`，由[liuhan](https://github.com/liuhan907)编写完成。</br>
首先将插件程序放到系统 PATH路径下(比如Linux下的/usr/bin)，然后执行代码生成，比如（在具体的服务目录里，比如`gayrpc/examples/echo/pb`）:
```sh
 protoc  -I. -I../../../src --cpp_out=. echo_service.proto
 protoc  -I. -I../../../src --gayrpc_out=. echo_service.proto
```

## Example
https://github.com/IronsDu/gayrpc/tree/master/examples

## Benchmark
Latency(single threaded):
```sh
connection num:1000
cost 16491 ms for 3000000 requests
throughput(TPS):187500
mean:5 ms, 5274983 ns
median:5 ms, 5132642 ns
max:57 ms, 57525470 ns
min:0 ms, 17231 ns
p99:19 ms, 19198630 ns
```

Throughput(two threaded):
```sh
Ubuntu 18.04 (i5 CPU)下，echo 300k QPS，当并发echo时 1000K QPS.
```

## 协议
目前实现的RPC通信协议底层采用两层协议.(注意!RPC核心库并不依赖具体通信协议!)
第一层采用二进制协议,且字节序统一为大端.
通信格式如下:

    [data_len | op | data]
    字段解释:
    data_len : uint64_t;
    op       : uint32_t;
    data     : char[data_len];

当`op`值为1时表示RPC消息,此为第二层协议!这时第一层协议中的data的内存布局则为:
    
    [meta_size | data_size | meta | data]
    字段解释:
    meta_size  : uint32_t;
    data_size  : uint64_t;
    meta       : char[meta_size];
    data       : char[data_size];

其中`meta`为 `RpcMata`的binary.`data`为某业务上的Protobuf Request或Response类型对象的binary或JSON.

`RpcMata`的proto定义如下:
```protobuf
syntax = "proto3";

package gayrpc.core;

message RpcMeta {
    enum Type {
        REQUEST = 0;
        RESPONSE = 1;
    };

    enum DataEncodingType {
        BINARY = 0;
        JSON = 1;
    };

    message Request {
        // 请求的服务函数
        uint64  method = 1;
        // 请求方是否期待服务方返回response
        bool    expect_response = 2;
        // 请求方的序号ID
        uint64  sequence_id = 3;
    };

    message Response {
        // 请求方的序号ID
        uint64  sequence_id = 1;
        // 执行是否成功
        bool    failed = 2;
        // (当failed为true)错误码
        int32   error_code = 3;
        // (当failed为true)错误原因
        string  reason = 4;
    };
    
    // Rpc类型(请求、回应)
    Type    type = 1;
    // RpcData的编码方式
    DataEncodingType encoding = 2;
    // 请求元信息
    Request request_info = 3;
    // 回应元信息
    Response response_info = 4;
}
```

## 服务描述文件范例
以下面的服务定义为例:
```protobuf
syntax = "proto3";

package dodo.test;

message EchoRequest {
    string message = 1;
}

message EchoResponse {
    string message = 1;
}

service EchoServer {
    rpc Echo(EchoRequest) returns(EchoResponse){
        option (gayrpc.core.message_id)= 1 ;//设定消息ID,也就是rpc协议中request_info的method
    };
}
```

## 处理请求或Response的实现原理
1. 编写第一层通信协议的编解码
2. 将第一层中的`data`作为第二层协议数据,反序列化其中的`meta`作为`RpcMeta`对象
3. 判断`RpcMata`中的`type`
    1. 如果为`REQUEST`则根据`request_info`中的元信息调用`method`所对应的服务函数.
        此时第二层协议中的`data`则为服务函数的请求请求对象(比如`EchoRequest`).
    2. 如果为`RESPONSE`则根据`response_info`中的元信息调用`sequence_id`对应的回调函数.
        此时第二层协议中的`data`则为服务方返回的Response(比如`EchoResponse`)

## 发送请求的实现原理
以`client->echo(echoRequest, responseCallback)`为例
参考代码:[GayRpcClient.h](https://github.com/IronsDu/gayrpc/blob/master/include/GayRpcClient.h#L34)

1. 客户端分配 sequence_id,以它为key将 responseCallback保存起来.
2. 将echoRequest序列化为binary作为第二层协议中的`data`
3. 构造RpcMeta对象,将echo函数对应的id号作为request_info的method,并设置sequence_id.
4. 将RpcMeta对象的binary作为第二层协议中的`meta`
5. 用第二层协议的数据写入第一层协议进行发送给服务端.

## 发送Response的实现原理
以`replyObj->reply(echoResponse)`为例
参考代码:[GayRpcReply.h](https://github.com/IronsDu/gayrpc/blob/master/include/GayRpcReply.h#L31)

1. 首先replyObj里(拷贝)储存了来自于请求中的RpcMata对象.
2. 将echoResponse序列化为binary作为第二层协议中的`data`
3. 构造RpcMeta对象,将备份的RpcMeta对象中的sequence_id设置到前者中response_info的sequence_id.
4. 将RpcMeta对象的binary作为第二层协议中的`meta`
5. 用第二层协议的数据写入第一层协议进行发送给服务端.

## 编解码参考
https://github.com/IronsDu/gayrpc/tree/master/src/gayrpc/protocol

## 注意点
* RPC核心并不依赖通信采用的协议,而且网络传输可以是TCP、UDP、WebSocket等等，亦或消息队列等等。
* RPC服务方的reply顺序与客户端的调用顺序无关,也就是可能后发起的请求先得到返回.
