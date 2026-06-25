#!/bin/bash

DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${DEPLOY_DIR}"

mkdir -p logs

PID_FILE="logs/sip-answer-engine.pid"
STDOUT_LOG="logs/stdout.log"

# 检查服务是否已经在运行
if [ -f "${PID_FILE}" ]; then
    PID=$(cat "${PID_FILE}")
    if kill -0 "${PID}" 2>/dev/null; then
        echo "提示：sip-answer-engine 服务已在运行中 (PID: ${PID})，请勿重复启动。"
        exit 1
    fi
fi

# 确保二进制程序存在
if [ ! -f "bin/sip-answer-engine" ]; then
    echo "错误：未找到 bin/sip-answer-engine，请先运行 ./build.sh 进行编译。"
    exit 1
fi

echo "正在启动 sip-answer-engine..."
# 启动程序并将日志重定向到 logs/stdout.log，重定向 stdin，并使用 nohup 忽略 HUP 信号
nohup ./bin/sip-answer-engine --config config.yaml > "${STDOUT_LOG}" 2>&1 < /dev/null &
PID=$!

# 保存 PID
echo "${PID}" > "${PID_FILE}"

# 验证进程是否启动成功
sleep 0.5
if kill -0 "${PID}" 2>/dev/null; then
    echo "服务启动成功！"
    echo "- PID: ${PID}"
    echo "- 运行日志请查看: ${DEPLOY_DIR}/${STDOUT_LOG}"
else
    echo "错误：服务启动失败，详细报错请检查日志: ${DEPLOY_DIR}/${STDOUT_LOG}"
    rm -f "${PID_FILE}"
    exit 1
fi
