#!/bin/bash
# Run one or more integration suites under the gate lock.
#   tools/suiten.sh fx routing presentation
#
# 🔴 Exit codes are DISTINCT on purpose (2026-09-22): a one-liner of the shape
#   `ruff check && exec 9>lock && flock -n 9 || { echo BELEGT; exit 3; }`
# reports "BELEGT" when RUFF failed — the && chain skips the `exec`, so flock runs on an
# unopened fd and fails for the wrong reason. Losing an hour to a lock that was never held
# is exactly the kind of misleading message this repo keeps fixing in its test helpers.
#   1 = lint failed   3 = gate is really busy   0 = all suites green   4 = a suite went red
cd "$(dirname "$0")/.."

if ! ruff check tests/; then
    echo "ABBRUCH: ruff (nicht die Sperre)"
    exit 1
fi

exec 9>build/.gate.lock
if ! flock -n 9; then
    echo "ABBRUCH: Gate-Sperre wird wirklich gehalten"
    exit 3
fi

pkill -f kmixdeckd 2>/dev/null
sleep 2
echo "Start $(date +%H:%M:%S) load $(cut -d' ' -f1 /proc/loadavg) frei $(free -m | awk '/Mem:/{print $7}')M"

rot=0
for s in "$@"; do
    log=/var/tmp/suite_$s.log
    timeout 900 python3 -m pytest "tests/integration/test_$s.py" -q -rA > "$log" 2>&1
    rc=$?
    echo "$s exit=$rc $(tail -1 "$log")"
    lecks=$(grep -c 'cleanup did not finish' "$log" 2>/dev/null || echo 0)
    [ "$lecks" != "0" ] && grep 'cleanup did not finish' "$log" | head -3
    [ "$rc" != "0" ] && { rot=1; grep -E '^E ' "$log" | head -4; }
done
exit $((rot * 4))
