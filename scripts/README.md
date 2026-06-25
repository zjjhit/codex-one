# Stress Test Scripts

## 1000-concurrency 10-hour test

Run from the repository root after the service is already started:

```bash
chmod +x scripts/stress_1000_10h.sh
./scripts/stress_1000_10h.sh
```

Defaults:

- Target SIP service: `127.0.0.1:5060`
- Called number: `8881000`
- Scenario: `tests/sipp/uac_invite.xml`
- Duration: `36000` seconds
- Batch size: `1000` calls
- Call rate: `100` calls/second
- Metrics interval: `30` seconds

Override examples:

```bash
TARGET=10.10.1.23:5060 SERVICE=8881000 ./scripts/stress_1000_10h.sh
```

Short dry run:

```bash
DURATION_SECONDS=300 BATCH_CALLS=100 CALL_RATE=20 ./scripts/stress_1000_10h.sh
```

Results are written to:

```text
stress-results/<timestamp>/
```

Key files:

- `summary.log`: batch start/end and final summary
- `monitor.log`: periodic `/metrics`, `/runtime`, `uptime`, `free`, and `ss -s`
- `sipp-batch-*.log`: SIPp output per batch
- `metrics.end.json`: final service metrics
- `runtime.end.json`: final runtime state
