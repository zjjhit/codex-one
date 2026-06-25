# SIP实时监听模拟应答引擎

一个面向语音线路压力测试的独立 Linux 服务：监听 SIP 呼入，按被叫前缀过滤，自动应答，随机保持通话，再主动挂断并记录完整日志。

## 当前实现范围

- 内嵌最小 SIP UDP 引擎：支持 `INVITE`、`ACK`、`BYE`、`CANCEL`、前缀过滤、概率接通、每日上限和随机保持。
- HTTP 管理接口：`/health`、`/metrics`、`/config`、`/calls`、`/admin/reload`、`/admin/reset-daily-counter`。
- 配置热更新：`PUT /config` 持久化到 `config.yaml`，已建立通话不受影响。
- 日志存储：结构化 JSON Lines；如果编译环境存在 SQLite3，自动额外写入 `logs/calls.db`。
- 目标运行环境：Linux。PJSIP/PJMEDIA 的生产级媒体栈可在 `SipEngine` 边界替换接入。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行

```bash
./build/sip-answer-engine --config config.yaml
```

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

## v0.3 Deployment and Stress Checks

The v0.3 branch keeps the v0.2 stress enhancements and adds Kylin/Linux deployment packaging scripts.

Cloud deployment and peer interconnect documents:

- `docs/cloud-deployment-and-interconnect.md`
- `docs/peer-interconnect-checklist.md`
- `deploy/cloud-config.example.yaml`
- `deploy/sip-answer-engine.service`


Recommended validation on Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/sip-answer-engine --config config.yaml
```

Smoke test:

```bash
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 1 -r 1
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/runtime
curl 'http://127.0.0.1:8080/calls?limit=10'
```

Concurrency checks:

```bash
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 100 -r 20
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 1000 -r 100
```

Operational controls:

```bash
curl -X POST http://127.0.0.1:8080/admin/drain
curl -X POST http://127.0.0.1:8080/admin/resume
```

Acceptance indicators:

- `current_concurrent` returns to `0` after SIPp completes.
- `rtp_ports_in_use` returns to `0` and `rtp_ports_available` returns to the configured pool size.
- `log_dropped` stays `0` during normal 1000-call tests.
- The daily JSONL log file is created as `logs/calls-YYYY-MM-DD.jsonl`.
