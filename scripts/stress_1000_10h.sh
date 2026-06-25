#!/usr/bin/env bash
set -euo pipefail

# 10-hour SIPp stability test for sip-answer-engine v0.2.
# Run from the repository root on the cloud host.

TARGET="${TARGET:-127.0.0.1:5060}"
SERVICE="${SERVICE:-8881000}"
SCENARIO="${SCENARIO:-tests/sipp/uac_invite.xml}"
HTTP_BASE="${HTTP_BASE:-http://127.0.0.1:8080}"

DURATION_SECONDS="${DURATION_SECONDS:-36000}"
BATCH_CALLS="${BATCH_CALLS:-1000}"
CALL_RATE="${CALL_RATE:-100}"
PAUSE_SECONDS="${PAUSE_SECONDS:-5}"

RUN_ID="${RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
OUT_DIR="${OUT_DIR:-stress-results/$RUN_ID}"
METRICS_INTERVAL_SECONDS="${METRICS_INTERVAL_SECONDS:-30}"

mkdir -p "$OUT_DIR"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 2
  fi
}

require_cmd sipp
require_cmd curl
require_cmd date
require_cmd awk

if [[ ! -f "$SCENARIO" ]]; then
  echo "scenario file not found: $SCENARIO" >&2
  exit 2
fi

echo "stress test started at $(date -Is)" | tee "$OUT_DIR/summary.log"
echo "target=$TARGET service=$SERVICE duration=${DURATION_SECONDS}s batch_calls=$BATCH_CALLS rate=$CALL_RATE" | tee -a "$OUT_DIR/summary.log"
echo "output directory: $OUT_DIR" | tee -a "$OUT_DIR/summary.log"

curl -fsS "$HTTP_BASE/health" | tee "$OUT_DIR/health.start.json" >/dev/null
curl -fsS "$HTTP_BASE/runtime" | tee "$OUT_DIR/runtime.start.json" >/dev/null
curl -fsS "$HTTP_BASE/metrics" | tee "$OUT_DIR/metrics.start.json" >/dev/null

stop_requested=0
on_stop() {
  stop_requested=1
  echo "stop requested at $(date -Is)" | tee -a "$OUT_DIR/summary.log"
}
trap on_stop INT TERM

collect_metrics() {
  while [[ "$stop_requested" -eq 0 ]]; do
    ts="$(date -Is)"
    {
      echo "### $ts metrics"
      curl -fsS "$HTTP_BASE/metrics" || true
      echo
      echo "### $ts runtime"
      curl -fsS "$HTTP_BASE/runtime" || true
      echo
      echo "### $ts host"
      uptime || true
      free -m || true
      ss -s || true
      echo
    } >> "$OUT_DIR/monitor.log"
    sleep "$METRICS_INTERVAL_SECONDS"
  done
}

collect_metrics &
monitor_pid=$!

start_epoch="$(date +%s)"
end_epoch=$((start_epoch + DURATION_SECONDS))
batch=0
total_exit_0=0
total_exit_nonzero=0

while [[ "$(date +%s)" -lt "$end_epoch" && "$stop_requested" -eq 0 ]]; do
  batch=$((batch + 1))
  now="$(date -Is)"
  log_file="$OUT_DIR/sipp-batch-$batch.log"
  echo "batch $batch started at $now" | tee -a "$OUT_DIR/summary.log"

  set +e
  sipp "$TARGET" \
    -sf "$SCENARIO" \
    -s "$SERVICE" \
    -m "$BATCH_CALLS" \
    -r "$CALL_RATE" \
    -trace_err \
    -trace_stat \
    > "$log_file" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -eq 0 ]]; then
    total_exit_0=$((total_exit_0 + 1))
  else
    total_exit_nonzero=$((total_exit_nonzero + 1))
  fi

  echo "batch $batch finished rc=$rc at $(date -Is)" | tee -a "$OUT_DIR/summary.log"
  sleep "$PAUSE_SECONDS"
done

stop_requested=1
wait "$monitor_pid" 2>/dev/null || true

curl -fsS "$HTTP_BASE/runtime" | tee "$OUT_DIR/runtime.end.json" >/dev/null || true
curl -fsS "$HTTP_BASE/metrics" | tee "$OUT_DIR/metrics.end.json" >/dev/null || true
curl -fsS "$HTTP_BASE/calls?limit=20" | tee "$OUT_DIR/calls.tail.json" >/dev/null || true

{
  echo "stress test ended at $(date -Is)"
  echo "batches=$batch"
  echo "successful_sipp_batches=$total_exit_0"
  echo "failed_sipp_batches=$total_exit_nonzero"
  echo "expected_calls_per_batch=$BATCH_CALLS"
  echo "target=$TARGET"
  echo "service=$SERVICE"
  echo "results=$OUT_DIR"
} | tee -a "$OUT_DIR/summary.log"

if [[ "$total_exit_nonzero" -gt 0 ]]; then
  echo "one or more SIPp batches failed; inspect $OUT_DIR" >&2
  exit 1
fi
