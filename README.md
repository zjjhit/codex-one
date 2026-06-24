# SIP实时监听模拟应答引擎

一个面向语音线路压力测试的独立 Linux 服务：监听 SIP 呼入，按被叫前缀过滤，自动应答，随机保持通话，再主动挂断并记录完整日志。

## 当前实现范围

- 内嵌最小 SIP UDP 引擎：支持 `INVITE`、`ACK`、`BYE`、`CANCEL`、前缀过滤、概率接通、每日上限和随机保持。
- HTTP 管理接口：`/health`、`/metrics`、`/config`、`/calls`、`/admin/reload`、`/admin/reset-daily-counter`。
- 配置热更新：`PUT /config` 持久化到 `config.yaml`，已建立通话不受影响。
- 日志存储：结构化 JSON Lines；如果编译环境存在 SQLite3，自动额外写入 `logs/calls.db`。
- 目标运行环境：Linux。PJSIP/PJMEDIA 的生产级媒体栈可在 `SipEngine` 边界替换接入。

## 构建与打包

可以使用根目录下的 `build.sh` 脚本一键完成编译和部署目录打包：

```bash
./build.sh
```

脚本执行成功后会在项目根目录下生成部署目录 `dist/`。

## 部署说明

您可以将打包生成的 `dist/` 目录中的内容复制到目标服务器进行部署。部署目录的结构如下：

```text
dist/
├── bin/
│   └── sip-answer-engine     # 编译后的引擎二进制程序
├── config.yaml               # 配置文件
├── start.sh                  # 服务启动脚本
├── stop.sh                   # 服务停止脚本
├── test.sh                   # SIPp 压测测试脚本
└── tests/                    # 测试资源文件
    └── sipp/
        └── uac_invite.xml
```

### 服务管理

在部署目录下，您可以通过以下命令来管理服务的启动、停止和测试：

- **启动服务**：
  ```bash
  ./start.sh
  ```
  服务将在后台运行，PID 将写入 `logs/sip-answer-engine.pid`，标准输出与错误日志将输出至 `logs/stdout.log`。

- **停止服务**：
  ```bash
  ./stop.sh
  ```
  停止脚本将优雅地终止服务（最多等待 5 秒），如果超时仍未退出，则会强制结束进程。

- **运行压测**：
  ```bash
  ./test.sh [目标IP] [总呼叫数] [呼叫速率]
  ```
  例如：`./test.sh 127.0.0.1 100 10`。如果部署环境中没有安装 `docker`，脚本会自动提示本地压测所使用的 `sipp` 对应指令。

## 运行

默认监听：

- SIP UDP：`0.0.0.0:5060`
- HTTP：`0.0.0.0:8080`
- 被叫前缀：`888`

## API 示例

```bash
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/config
curl -X PUT http://127.0.0.1:8080/config \
  -H 'Content-Type: application/json' \
  -d '{"answer_rate_percent":50,"min_hold_seconds":1,"max_hold_seconds":15}'
curl http://127.0.0.1:8080/calls?limit=20
curl -X POST http://127.0.0.1:8080/admin/reload
curl -X POST http://127.0.0.1:8080/admin/reset-daily-counter
```

## SIPp 压测

仓库提供了基础场景文件：

```bash
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 1000 -r 100
```

## 生产化接入建议

当前最小 SIP UDP 引擎用于快速闭环和压测验证。正式接入运营级线路时，建议在不改变 `CallManager`、配置、日志和 HTTP 控制面的前提下，将 `SipUdpEngine` 替换为 PJSIP/PJMEDIA 适配器，以获得完整注册、鉴权、NAT、SDP/RTP 兼容性和更强的协议容错能力。
