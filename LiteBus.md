# LiteBus 执行流程与调用链深度解析

## 高层架构与运行模型
- **Actor 层**：`ActorBase` 定义消息收发、handler 注册、生命周期回调；`ActorMgr` 负责维护 actor 表、线程池（`ActorThread`）以及 IO 管理器映射。【F:common/litebus/include/actor/actor.hpp†L45-L126】【F:common/litebus/src/actor/actormgr.cpp†L128-L175】
- **线程调度**：`ActorThread` 维护一个工作线程池与就绪队列；`ShardedThread` 将多个 actor 复用在共享线程池；`SingleThread` 独占线程且用条件变量阻塞等待消息。【F:common/litebus/src/actor/actorthread.cpp†L41-L118】【F:common/litebus/src/actor/actorpolicy.cpp†L21-L118】
- **IO 层**：抽象接口 `IOMgr` 定义网络发送/接收、链接管理、消息回调注册等；`TCPMgr`、`HttpIOMgr`（可选 `UDP`/`SSL`）提供具体实现并在 `StartServer` 时被实例化并挂入 `ActorMgr`。
- **系统管理**：初始化阶段创建 `SysMgrActor` 负责系统级定时与心跳；HTTP 模式下还会 `Spawn` `HttpSysMgr`。【F:common/litebus/src/litebus.cpp†L226-L276】【F:common/litebus/src/httpd/http_iomgr.cpp†L391-L395】

整体流程：`Initialize` 解析配置→根据 URL 创建对应 `IOMgr` 并启动监听→构建线程池→注册消息回调→`Spawn` 系统 actor；业务通过 `Spawn` 注册自定义 actor，消息经 `ActorMgr::Send` 判断本地/远程走本地队列或 IO 管道。

## `Spawn(actor)` 调用链详解
1. **入口**：`litebus::Spawn(ActorReference actor, bool sharedThread, bool start)` 暴露在 `litebus.cpp`。【F:common/litebus/src/litebus.cpp†L304-L315】
2. **调度至 ActorMgr**：若未 Finalize，则转到 `ActorMgr::Spawn`。【F:common/litebus/src/actor/actormgr.cpp†L177-L209】
3. **线程策略选择**：
   - `sharedThread=true`：创建 `ShardedThread`，调用 `ActorBase::Spawn` 绑定，actor 初始不立即入队，等待第一条消息或 `Notify` 标记 ready。【F:common/litebus/src/actor/actorpolicy.cpp†L69-L118】
   - `sharedThread=false`：使用 `SingleThread` 独占线程，`SetActorReady` 立即把 actor 入队。【F:common/litebus/src/actor/actormgr.cpp†L189-L198】
4. **注册与初始化**：
   - 将 actor 记录到 `actors` map，随后调用用户覆写的 `Init()` 完成 handler 注册等工作。【F:common/litebus/src/actor/actormgr.cpp†L200-L205】
   - 根据 `start` 触发 `ActorPolicy::SetRunningStatus`，共享线程的就绪检查由 `ShardedThread::Notify/EnqueMessage` 驱动；独占线程使用条件变量唤醒。【F:common/litebus/src/actor/actorpolicy.cpp†L21-L68】【F:common/litebus/src/actor/actorpolicy.cpp†L85-L118】
5. **运行与退出**：
   - `ActorThread::Run` 从就绪队列取出 actor，循环调用 `ActorBase::Run` 处理消息，直到收到 `KTERMINATE` 或线程退出信号。【F:common/litebus/src/actor/actorthread.cpp†L89-L117】【F:common/litebus/include/actor/actor.hpp†L106-L170】
   - 退出时 `ActorBase::Quit` 释放资源并通过 `ActorPolicy::Terminate` 从 `ActorMgr` 移除。【F:common/litebus/src/actor/actor.cpp†L62-L117】【F:common/litebus/src/actor/actorpolicy.cpp†L31-L68】

### `Spawn(actor)` 关键调用链（逐函数）
```
litebus::Spawn
  → ActorMgr::Spawn
      → Actor::Spawn（选择 ShardedThread 或 SingleThread）
          → ActorMgr::SetActorReady（独占线程立即入队）
      → Actor::Init（用户注册 handler）
      → Actor::SetRunningStatus
ActorThread::Run（线程池消费就绪 actor）
  → ActorBase::Run（从 mailbox 取 MessageBase）
      → HandlekMsg/HandlekHttp 等分支
          → 用户 handler（Receive 注册的方法）
```

### 消息处理与分发
- **本地路径**：`ActorMgr::Send` 判断 `IsLocalAddress`，本地 actor 直接 `EnqueMessage`，并填充 `peer` 便于签名校验。【F:common/litebus/src/actor/actormgr.cpp†L142-L175】
- **远程路径**：设置 `to` 字段后调用对应协议的 `IOMgr::Send`（TCP/UDP/HTTP），完成序列化与网络传输。【F:common/litebus/src/actor/actormgr.cpp†L165-L175】【F:common/litebus/src/actor/iomgr.hpp†L31-L66】
- **handler 注册**：actor 内调用 `Receive` 将 `msgName` 映射到 `ActorFunction`；`ActorBase::Run` 根据 `MessageBase::Type` 分派到 KMSG/KHTTP/KUDP/本地/退出等分支，最终调用对应 handler。【F:common/litebus/src/actor/actor.cpp†L38-L121】【F:common/litebus/include/actor/actor.hpp†L80-L170】

### ASCII 时序（共享线程）
```
Spawn(actor)
  └─ActorMgr::Spawn
      ├─new ShardedThread
      ├─actor->Init()
      └─SetRunningStatus(start)
           └─ShardedThread::Notify -> SetActorReady(actor)
                └─ActorThread::Run loop
                      └─ActorBase::Run -> handler
```

## `Spawn(https)` 调用链（HTTP/HTTPS 管线）
> LiteBus 在 HTTP 入口时会为每个客户端连接动态 `Spawn` 一个 `HttpPipelineProxy` actor；HTTPS 走同一套 HTTP IO 流程，区别在于 `TCPMgr`/`HttpIOMgr` 使用 SSL 封装。

1. **HTTP I/O 入口**：`HttpIOMgr::HandleRequest` 在解析到新连接且无现成代理时调用 `Spawn(httpPipelineProxy)`，为该连接创建 pipeline actor。【F:common/litebus/src/httpd/http_iomgr.cpp†L317-L339】
2. **HTTP 系统管理**：`HttpIOMgr::EnableHttp` 在 LiteBus 初始化 TCP IOMgr 时调用，注册 HTTP 收包回调并 `Spawn` `HttpSysMgr` 管理 HTTP 侧定时任务。【F:common/litebus/src/httpd/http_iomgr.cpp†L391-L395】
3. **客户端连接（HTTP/HTTPS）**：`HttpConnect::HttpConnection` 构造时 `Spawn` `HttpConnectionActor`，用于单条请求链路；若 URL scheme 为 `https` 且编译开启 `SSL_ENABLED`，HTTP 连接层使用 SSL socket，但调用链保持一致。【F:common/litebus/src/httpd/http_connect.cpp†L236-L276】【F:common/litebus/src/httpd/http_connect.cpp†L200-L235】
4. **消息分发**：`HttpPipelineProxy::Process`（在 actor 内）把 HTTP 请求封装为 `HttpMessage`（Type=KHTTP），通过 `msgHandler` 回调进入 `ActorMgr::Receive`，再分派到目标 actor handler，响应通过 pipeline 逐条写回，保证 HTTP/1.1 顺序。【F:common/litebus/src/httpd/http_iomgr.cpp†L341-L347】【F:common/litebus/src/httpd/http_iomgr.cpp†L300-L339】

### HTTP/HTTPS 入口关键调用链（从 socket 到 handler）
```
TCPMgr::StartIOServer（监听并注册 epoll）
  → tcpUtil::OnAccept（EPOLLIN 触发 accept）【F:common/litebus/src/tcp/tcpmgr.cpp†L150-L194】
      → ConnectionUtil::AddNewConnEventHandler（注册读事件）
        → iomgrUtil::RecvHttpReq 回调到 HttpIOMgr::HandleRequest【F:common/litebus/src/httpd/http_iomgr.cpp†L349-L366】
            → Spawn(HttpPipelineProxy)（按连接生成 actor）
            → Async(HttpPipelineProxy::Process) 封装 HttpMessage
                → msgHandler → ActorMgr::Receive → 目标 ActorBase::Run
                    → handler 执行并写入 Promise<Response>
                → HttpPipelineProxy 顺序写回响应
```

### HTTPS 传输链底层
- 网络层仍由 `TCPMgr` 管理，`Init()` 时根据 `LITEBUS_HTTPKMSG_ENABLED` 和 `SetHttpKmsgFlag` 决定解析模式并创建 `recvEvloop`/`sendEvloop`（基于 epoll）。【F:common/litebus/src/tcp/tcpmgr.cpp†L89-L160】【F:common/litebus/src/tcp/tcpmgr.cpp†L241-L311】
- SSL 模式下 `TCPMgr::ConnEstablishedSSL` 触发降级/重连，socket 操作由 `ssl::SslSocketOperate`（未展开）替换；否则使用 `TCPSocketOperate`。
- 接收路径：`EPOLLIN` 触发后 `RecvMsg` 根据 `ParseType` 调用 `RecvKMsg` 或 HTTP 回调；发送路径使用 `ConnectionSend` 准备消息缓冲并通过 `sendmsg` 写出。【F:common/litebus/src/tcp/tcpmgr.cpp†L300-L375】【F:common/litebus/src/tcp/tcpmgr.cpp†L176-L219】

## 底层通信与序列化
- **TCP send/recv**：`TCPSocketOperate` 封装系统调用 `recv`、`recvmsg`、`sendmsg`，处理 `EAGAIN`/连接重置等错误并记录错误码，使用 `MSG_NOSIGNAL` 避免 SIGPIPE。【F:common/litebus/src/tcp/tcp_socket.cpp†L60-L155】【F:common/litebus/src/tcp/tcp_socket.cpp†L157-L240】
- **事件循环**：`EvLoop`（epoll 封装，未展开）在 `TCPMgr::StartIOServer` 内注册 server fd 读事件；客户端连接与写事件通过 `recvEvloop`/`sendEvloop` 双循环分离收发负载。【F:common/litebus/src/tcp/tcpmgr.cpp†L311-L362】
- **消息封包**：`EvbufMgr::PrepareSendMsg` 将 `MessageBase` 序列化为 wire 格式（包括魔数、长度、AID 信息），随后进入 send 队列；反向 `ConnectionUtil::RecvKMsg` 解析并创建 `MessageBase` 对象，经 `tcpMsgHandler` 回到 `ActorMgr::Receive`（在 `StartServer` 注册）。【F:common/litebus/src/tcp/tcpmgr.cpp†L162-L220】【F:common/litebus/src/litebus.cpp†L132-L183】

### KMSG 网络收发关键调用链
```
Actor handler/ActorMgr::Send（判定本地/远程）
  → IOMgr::Send（TCPMgr 实现，入连接 sendQueue）
      → tcpUtil::ConnectionSend（逐条 EvbufMgr::PrepareSendMsg）【F:common/litebus/src/tcp/tcpmgr.cpp†L196-L232】
          → TCPSocketOperate::Sendmsg（系统调用 sendmsg）【F:common/litebus/src/tcp/tcp_socket.cpp†L157-L200】
TCP epoll 读事件
  → ConnectionUtil::RecvKMsg（解析魔数/长度）
      → tcpMsgHandler（litebus 注册回调）【F:common/litebus/src/litebus.cpp†L132-L183】
          → ActorMgr::Receive → Actor::EnqueMessage
              → ActorBase::Run → HandlekMsg → handler
```

## handler 注册与参数流
1. **注册**：用户 actor 在 `Init()` 中调用 `Receive("msg", &T::Method)`，记录到 `actionFunctions` map。【F:common/litebus/src/actor/actor.cpp†L84-L117】
2. **收包**：网络层解析后构造 `MessageBase`（包含 `from/to/name/body`、签名等）并调用 `ActorMgr::Receive`；本地发送则直接入队。
3. **解包与分发**：`ActorBase::Run` 逐条处理 `MessageBase`，根据 `type` 分支到 TCP(`KMSG`)/UDP(`KUDP`)/HTTP(`KHTTP`)/异步/本地/退出；`HandlekMsg` 通过 `msg->Name()` 找到 handler 并传递 `from/name/body`，HTTP/UDP 经过对应包装保证类型安全。【F:common/litebus/src/actor/actor.cpp†L96-L170】
4. **响应**：handler 内可调用 `Send` 构造 `MessageBase` 并经 `ActorMgr::Send` 走本地或远程；HTTP 流程通过 `Promise<Response>` 写回 pipeline，TCP/UDP 直接编码发送。

## 生命周期、容错与线程模型
- **生命周期**：`Initialize` 设置线程数、启动 IO/定时器、创建系统 actor；`Finalize` 标记退出、终止全部 actor、回收线程池和 IO；`LiteBusExit` 保证进程退出时自动清理。【F:common/litebus/src/litebus.cpp†L226-L360】
- **可靠性/顺序性**：每个 actor 的消息队列在 `ActorPolicy` 内保持 FIFO；`HttpPipelineProxy` 保证同连接的 HTTP 响应顺序；TCP 发送采用单连接序列化，UDP 不保证可靠性但可设置包大小规则。【F:common/litebus/src/httpd/http_iomgr.cpp†L300-L347】【F:common/litebus/src/actor/actorpolicy.cpp†L69-L118】
- **超时与错误处理**：
  - 连接层遇到 `recv`/`send` 异常将 `connState` 置为 `DISCONNECTING` 并触发 `eventCallBack` 关闭链接；支持自动重连/降级（SSL）。【F:common/litebus/src/tcp/tcpmgr.cpp†L300-L375】
  - HTTP 客户端侧在 `HttpConnectionActor` 中通过 `AsyncAfter` 创建响应超时定时器，超时会 `Disconnect` 并回调 promise（代码在同文件前段）。
- **线程模型**：
  - 共享线程：所有 `ShardedThread` actor 共享 `ActorThread` 池，通过 `SetActorReady` 入队，消息批量交换 `mailbox` 后顺序消费。
  - 独占线程：`SingleThread` 中 `condition_variable` 阻塞等待消息，适合重计算 actor。
  - IO 线程：`recvEvloop`/`sendEvloop`/`UDP`/`HTTP` 等均自带独立线程与 epoll 循环，与 actor 线程池解耦。


## function_proxy 示例：LiteBus 初始化 → ServerLoop → Handler 调用链
### 1. 初始化阶段（拉起 LiteBus 与核心 actor）
- **业务入口**：`function_proxy` 的 `OnCreate` 首先调用 `ModuleSwitcher::InitLiteBus`，拼装 `tcp://{address}` 并传入线程数，内部转到 `litebus::Initialize` 完成 LiteBus 全局初始化。【F:functionsystem/src/function_proxy/main.cpp†L420-L466】【F:functionsystem/src/common/utils/module_switcher.cpp†L46-L56】
- **LiteBus 初始化**：`litebus::Initialize` 调用 `InitializeImp`，依次执行：
  1. 忽略 `SIGPIPE` 与 SSL 初始化；
  2. `SetThreadCount` 启动 actor 线程池；
  3. 通过 `StartServer` 依据 URL 协议创建并注册对应 `IOMgr`；
  4. 注册消息回调 `ActorMgr::Receive`，并 `StartIOServer` 绑定监听；
  5. `Spawn(SysMgrActor)` 提供系统计时服务。【F:common/litebus/src/litebus.cpp†L132-L275】
- **业务 actor 拉起**：LiteBus 就绪后，`BusproxyStartup::Run` 先 `Spawn(RequestRouter)`、`Spawn(proxy::Actor)`（负责转发/注册），并写入注册中心。【F:functionsystem/src/function_proxy/busproxy/startup/busproxy_startup.cpp†L81-L118】

**初始化调用链（示例）**
```
main → OnCreate → ModuleSwitcher::InitLiteBus
  → litebus::Initialize → InitializeImp
      → SetThreadCount（创建 ActorThread 池）
      → StartServer(tcp://address, ..., ActorMgr::Receive)
          → SetServerIo(TCPMgr)
          → TCPMgr::Init → 创建 recvEvloop/sendEvloop
          → TCPMgr::StartIOServer（listen + epoll 注册）
      → Spawn(SysMgrActor)
  → BusproxyStartup::Run → Spawn(RequestRouter) & Spawn(proxy::Actor)
```

### 2. ServerLoop：网络监听与收包
- `StartIOServer` 监听地址，随后在 `recvEvloop` 上注册 `OnAccept` 作为 server fd 的 EPOLLIN 回调。【F:common/litebus/src/tcp/tcpmgr.cpp†L340-L379】
- `OnAccept` 接收新连接、创建 `Connection`，设置 `event/read/write` 回调并调用 `AddNewConnEventHandler` 将客户端 fd 加入 epoll 读事件；连接信息记录到 `LinkMgr`。【F:common/litebus/src/tcp/tcpmgr.cpp†L149-L194】
- 客户端 fd 上出现 EPOLLIN 时，`TCPMgr::ReadCallBack` 进入循环调用 `RecvMsg`，根据当前解析模式选择 `RecvKMsg` 或 HTTP 回调。【F:common/litebus/src/tcp/tcpmgr.cpp†L381-L453】
- `ConnectionUtil::RecvKMsg` 完成魔数/长度校验与反序列化，解析后的 `MessageBase` 通过注册的 `tcpMsgHandler` 回调到 `ActorMgr::Receive`。【F:common/litebus/src/tcp/tcpmgr.cpp†L418-L453】【F:common/litebus/src/litebus.cpp†L132-L183】

### 3. Handler：消息入队与业务处理
- `ActorMgr::Receive` 依据 `to` 选择本地/远程，KMSG 直接 `actor->EnqueMessage`，并在共享线程时调用 `SetActorReady` 唤醒。【F:common/litebus/src/actor/actormgr.cpp†L142-L209】
- `ActorThread::Run`/`SingleThread` 从队列取出 `MessageBase`，`ActorBase::Run` 根据 `type` 跳转到 `HandlekMsg`，再按 `msg->Name()` 查表执行业务 handler（如 `proxy::Actor` 中的转发逻辑）。【F:common/litebus/src/actor/actor.hpp†L106-L170】【F:common/litebus/src/actor/actor.cpp†L38-L121】

**收包到 handler 调用链（function_proxy 场景）**
```
EPOLLIN(server) → OnAccept → AddNewConnEventHandler
EPOLLIN(client fd) → TCPMgr::ReadCallBack → RecvMsg → ConnectionUtil::RecvKMsg
  → tcpMsgHandler(ActorMgr::Receive) → Actor::EnqueMessage/SetActorReady
      → ActorThread::Run → ActorBase::Run → HandlekMsg → proxy::Actor 业务 handler
```

=======
## 总结
LiteBus 以 Actor 模型为核心，通过 `Spawn` 将业务逻辑挂入线程池或独占线程；`ActorMgr` 将本地队列与协议 `IOMgr` 粘合，提供统一的消息分发。网络层使用 epoll + sendmsg/recvmsg（可选 SSL）承载 TCP/HTTP/UDP，HTTP/HTTPS 通过动态生成的 pipeline actor 保证连接内顺序与背压，整个链路自初始化到 Finalize 形成一条清晰的控制与数据流。
