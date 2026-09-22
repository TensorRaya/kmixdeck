#!/bin/bash
# Run the test gate while holding an exclusive lock, so nobody can rebuild the binary under it.
#
# Why this exists: on 2026-09-22 a full gate reported "2 of 23 failed" with five named failures in the last
# two suites — because the binary had been rebuilt five times during the run, twice with a deliberately
# sabotaged build for a counter-check. Late tests measured a different binary than early ones, so the result
# said nothing about the code. The gate takes the lock; tools/bau.sh refuses to build while it is held.
#
#   tools/gate.sh              full gate  (ctest, all labels)
#   tools/gate.sh -L fast      quick gate (the label ctest understands)
#   tools/gate.sh --wait       wait for a running gate instead of refusing
set -u -o pipefail
cd "$(dirname "$0")/.."
SPERRE="build/.gate.lock"
mkdir -p build

WARTEN=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --wait) WARTEN=1 ;;
        *) ARGS+=("$a") ;;
    esac
done

# The log name says which kind of run it was, so two logs never overwrite each other.
STEMPEL="$(date +%Y%m%d-%H%M%S)"
LOG="/var/tmp/gate-${STEMPEL}.log"

FLOCK_OPT="-n"
[ "$WARTEN" = "1" ] && FLOCK_OPT=""

exec 9>"$SPERRE"
# shellcheck disable=SC2086
if ! flock $FLOCK_OPT 9; then
    echo "gate: another gate is already running (lock $SPERRE held)." >&2
    echo "      Two gates on one machine oversubscribe dbus/pipewire and both results become worthless." >&2
    echo "      Use --wait to queue behind it, or look at the running one's log in /var/tmp/gate-*.log." >&2
    exit 3
fi

# Load check: integration tests start a daemon, pipewire and dbus per module. Above ~3 they start timing out
# for reasons that have nothing to do with the code (CONTRIBUTING: "a red run says nothing …").
LAST="$(cut -d' ' -f1 /proc/loadavg)"
KERNE="$(nproc)"
echo "gate: load ${LAST} on ${KERNE} cores, $(free -g | awk '/Mem:/{print $7}')G free — log ${LOG}"
if [ "$(cut -d. -f1 /proc/loadavg)" -ge 3 ]; then
    echo "gate: load ${LAST} is too high for integration tests; waiting up to 10 min for it to drop below 3" >&2
    for _ in $(seq 1 40); do
        sleep 15
        [ "$(cut -d. -f1 /proc/loadavg)" -lt 3 ] && break
    done
    echo "gate: starting at load $(cut -d' ' -f1 /proc/loadavg)"
fi

echo "gate: ruff"
ruff check tests/ || { echo "gate: ruff failed — fix that first" >&2; exit 1; }

echo "gate: ctest ${ARGS[*]-(all labels)}"
ctest --test-dir build --output-on-failure "${ARGS[@]+"${ARGS[@]}"}" > "$LOG" 2>&1
RC=$?
grep -E '% tests passed' "$LOG" || true
grep -E '\(Failed\)|\(Timeout\)' "$LOG" | head -10 || true
echo "gate: exit ${RC}, full log ${LOG}"
exit $RC
