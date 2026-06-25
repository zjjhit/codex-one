#!/bin/bash

DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${DEPLOY_DIR}"

# 默认配置参数
TARGET_IP="127.0.0.1"
TARGET_PORT="5060"
CALLED_NUMBER="8881000"
TOTAL_CALLS="10"
CALL_RATE="2"

# 允许用户通过位置参数或环境变量覆盖默认值
if [ ! -z "$1" ]; then TARGET_IP="$1"; fi
if [ ! -z "$2" ]; then TOTAL_CALLS="$2"; fi
if [ ! -z "$3" ]; then CALL_RATE="$3"; fi

echo "========================================="
echo "开始运行容器化 SIPp 压力测试..."
echo "- 目标 IP: ${TARGET_IP}:${TARGET_PORT}"
echo "- 被叫号码: ${CALLED_NUMBER}"
echo "- 总呼叫数: ${TOTAL_CALLS}"
echo "- 呼叫速率: ${CALL_RATE} 次/秒"
echo "========================================="

# 检查 Docker 命令及 Daemon 状态
if ! command -v docker &> /dev/null || ! docker info &> /dev/null; then
    echo "错误：未在当前系统检测到 docker 或 Docker 服务未运行，无法进行容器化测试。"
    echo "提示：如果你本地编译了 sipp，可以直接运行以下命令进行本地测试："
    echo "  ./bin/sipp ${TARGET_IP}:${TARGET_PORT} -sf tests/sipp/uac_invite.xml -s ${CALLED_NUMBER} -m ${TOTAL_CALLS} -r ${CALL_RATE}"
    exit 1
fi

# 容器化运行 SIPp
# 使用 --net=host 以支持容器直接访问主机的 127.0.0.1 端口
# 将本地 tests 目录映射到容器的 /tests 目录，以载入 XML 场景文件
docker run --rm --net=host \
    -v "${DEPLOY_DIR}/tests:/tests" \
    ctaloi/sipp \
    "${TARGET_IP}:${TARGET_PORT}" \
    -sf /tests/sipp/uac_invite.xml \
    -s "${CALLED_NUMBER}" \
    -m "${TOTAL_CALLS}" \
    -r "${CALL_RATE}" \
    -trace_err

echo "========================================="
echo "测试执行完毕！"
echo "你可以通过执行以下命令检查服务端的统计指标："
echo "  curl -s http://127.0.0.1:8080/metrics"
echo "========================================="
