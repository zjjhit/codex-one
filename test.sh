#!/bin/bash
set -euo pipefail

DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${DEPLOY_DIR}"

TARGET_IP="${1:-127.0.0.1}"
TOTAL_CALLS="${2:-10}"
CALL_RATE="${3:-2}"
TARGET_PORT="${TARGET_PORT:-5060}"
CALLED_NUMBER="${CALLED_NUMBER:-8881000}"
SCENARIO="tests/sipp/uac_invite.xml"

echo "========================================="
echo "Running SIPp test..."
echo "- Target: ${TARGET_IP}:${TARGET_PORT}"
echo "- Called number: ${CALLED_NUMBER}"
echo "- Total calls: ${TOTAL_CALLS}"
echo "- Call rate: ${CALL_RATE} cps"
echo "========================================="

if [ ! -f "${SCENARIO}" ]; then
    echo "Error: scenario file not found: ${SCENARIO}"
    exit 1
fi

if command -v sipp >/dev/null 2>&1; then
    sipp "${TARGET_IP}:${TARGET_PORT}" \
        -sf "${SCENARIO}" \
        -s "${CALLED_NUMBER}" \
        -m "${TOTAL_CALLS}" \
        -r "${CALL_RATE}" \
        -trace_err
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    docker run --rm --net=host \
        -v "${DEPLOY_DIR}/tests:/tests" \
        ctaloi/sipp \
        "${TARGET_IP}:${TARGET_PORT}" \
        -sf /tests/sipp/uac_invite.xml \
        -s "${CALLED_NUMBER}" \
        -m "${TOTAL_CALLS}" \
        -r "${CALL_RATE}" \
        -trace_err
else
    echo "Error: neither local sipp nor docker is available."
    echo "Install SIPp, or run with Docker available."
    exit 1
fi

echo "========================================="
echo "Test completed. Check service metrics with:"
echo "  curl -s http://127.0.0.1:8080/metrics"
echo "========================================="
