#!/bin/bash
# Run test_ports.py under the gate lock and report both results that matter:
# the test outcome AND whether the leak watchdog stayed quiet.
cd "$(dirname "$0")/.."
ruff check tests/ || exit 1
exec 9>build/.gate.lock
flock -n 9 || { echo "BELEGT — ein Gate laeuft"; exit 3; }
pkill -f kmixdeckd 2>/dev/null
sleep 2
echo "Start $(date +%H:%M:%S) load $(cut -d' ' -f1 /proc/loadavg)"
LOG=/var/tmp/k3_ports.log
timeout 900 python3 -m pytest tests/integration/test_ports.py -q -rA > "$LOG" 2>&1
echo "ports exit=$?"
echo "Leck-Meldungen: $(grep -c 'cleanup did not finish' "$LOG")"
grep 'cleanup did not finish' "$LOG" | head -5
tail -2 "$LOG"
