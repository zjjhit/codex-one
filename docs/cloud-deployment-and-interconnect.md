# v0.2 云主机部署与 SIP 联调指南

## 适用范围

v0.2 适合做“对方 SIP 平台直接呼入云主机”的联调：对方往我方云主机 `公网IP:5060/UDP` 发起 `INVITE`，服务自动应答、保持随机时长、主动挂断。

当前版本不作为完整 SIP 注册/鉴权客户端使用。如果对方要求我方先注册到对方 SIP 平台，需要后续接入 PJSIP/PJMEDIA 或补充注册逻辑。

## 云主机部署

Ubuntu/Debian 安装依赖：

```bash
sudo apt update
sudo apt install -y git build-essential cmake curl sipp
```

拉取 v0.2 分支：

```bash
git clone https://github.com/zjjhit/codex-one.git
cd codex-one
git checkout codex/v0.2-stress-enhancements
```

编译：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

准备配置：

```bash
cp deploy/cloud-config.example.yaml config.yaml
```

核心配置建议：

```yaml
sip_ip: 0.0.0.0
sip_port: 5060
sip_transport: udp
called_prefix: "888"
answer_rate_percent: 100
min_hold_seconds: 1
max_hold_seconds: 15
concurrency_limit: 1000
rtp_port_start: 40000
rtp_port_end: 42000
http_ip: 127.0.0.1
http_port: 8080
```

启动：

```bash
./build/sip-answer-engine --config config.yaml
```

健康检查：

```bash
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/runtime
curl http://127.0.0.1:8080/metrics
```

## 防火墙和安全组

推荐只允许对方平台源IP访问：

- UDP `5060`：SIP信令
- UDP `40000-42000`：RTP媒体
- TCP `8080`：不建议公网开放；如必须开放，只允许运维IP

Ubuntu `ufw` 示例：

```bash
sudo ufw allow from <对方SIP源IP> to any port 5060 proto udp
sudo ufw allow from <对方RTP源IP或网段> to any port 40000:42000 proto udp
sudo ufw allow from <运维IP> to any port 8080 proto tcp
```

## systemd 部署

创建运行用户并安装文件：

```bash
sudo useradd --system --home /opt/sip-answer-engine --shell /usr/sbin/nologin sipanswer
sudo mkdir -p /opt/sip-answer-engine /etc/sip-answer-engine
sudo cp -a . /opt/sip-answer-engine
sudo cp deploy/cloud-config.example.yaml /etc/sip-answer-engine/config.yaml
sudo chown -R sipanswer:sipanswer /opt/sip-answer-engine /etc/sip-answer-engine
sudo cp deploy/sip-answer-engine.service /etc/systemd/system/sip-answer-engine.service
sudo systemctl daemon-reload
sudo systemctl enable --now sip-answer-engine
```

查看状态：

```bash
systemctl status sip-answer-engine
journalctl -u sip-answer-engine -f
curl http://127.0.0.1:8080/health
```

## 联调流程

### 1. 本机单通自测

```bash
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 1 -r 1
curl 'http://127.0.0.1:8080/calls?limit=10'
```

验收：SIPp 显示 `Successful call = 1`，日志状态为 `answered`。

### 2. 本机小并发

```bash
sipp 127.0.0.1:5060 -sf tests/sipp/uac_invite.xml -s 8881000 -m 100 -r 20
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/runtime
```

验收：`current_concurrent` 回到 `0`，`rtp_ports_in_use` 回到 `0`，`log_dropped` 为 `0`。

### 3. 对方单通呼入

给对方的目标地址：

```text
sip:8881000@<我方公网IP>:5060;transport=udp
```

我方观察：

```bash
tail -f logs/calls-$(date +%F).jsonl
curl http://127.0.0.1:8080/metrics
```

验收：收到 `INVITE` 后自动 `200 OK`，1-15秒后我方主动 `BYE`，日志 `status=answered`。

### 4. 批量和并发

建议逐级提升：

- 10通
- 100通
- 500通
- 1000通

每轮后检查：

```bash
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/runtime
```

### 5. 异常场景

- 被叫前缀不匹配：预期 `rejected_prefix_mismatch`
- 接通率为 `0`：预期 `rejected_probability`
- 对方提前 `BYE/CANCEL`：预期 `remote_hangup` 或 `remote_cancelled`
- 达到每日上限：预期 `rejected_daily_limit`

## 常见问题

- 对方收不到媒体：确认 UDP `40000-42000` 已开放，且对方按 SDP 获取媒体端口。
- 对方严格校验 SDP/Contact：当前 `sip_ip: 0.0.0.0` 可能不适合公网联调，建议将 `sip_ip` 临时配置为云主机可达IP，或后续增加 `advertised_ip`。
- 管理接口访问不了：默认 `http_ip: 127.0.0.1` 只能本机访问，可以通过 SSH 登录后 curl。
- 对方要求注册：v0.2 不建议用于该模式，需要进入 PJSIP/PJMEDIA 适配阶段。
