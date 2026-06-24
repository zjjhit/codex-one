#!/bin/bash

DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${DEPLOY_DIR}"

PID_FILE="logs/sip-answer-engine.pid"

if [ ! -f "${PID_FILE}" ]; then
    echo "未找到运行中的服务 PID 文件，服务可能并未启动。"
    exit 0
fi

PID=$(cat "${PID_FILE}")

# 检查进程是否存在
if ! kill -0 "${PID}" 2>/dev/null; then
    echo "服务未在运行 (PID ${PID} 未找到)，清理残留的 PID 文件..."
    rm -f "${PID_FILE}"
    exit 0
fi

echo "正在优雅关闭 sip-answer-engine (PID: ${PID})..."
kill "${PID}"

# 循环等待最多 5 秒让进程退出
for i in {1..5}; do
    if ! kill -0 "${PID}" 2>/dev/null; then
        echo "服务已成功关闭。"
        rm -f "${PID_FILE}"
        exit 0
    fi
    sleep 1
done

# 如果超时未关闭，强制强杀
echo "警告：服务未能在 5 秒内正常关闭，正在强制结束进程..."
kill -9 "${PID}"
rm -f "${PID_FILE}"
echo "服务进程已强制结束。"
