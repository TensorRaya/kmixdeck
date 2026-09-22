#!/bin/bash
# Build the project — but refuse while a test gate holds the lock.
#
# Why: rebuilding during a gate swaps the binary under the running tests. Measured 2026-09-22: a full gate
# reported five failures in its last two suites purely because I rebuilt five times during the run (twice a
# deliberately sabotaged binary). The tests were fine, the measurement was ruined. See CONTRIBUTING.
#
#   tools/bau.sh               build with -j4
#   tools/bau.sh -j8           pass anything through to cmake --build
#   tools/bau.sh --force       build anyway (you are knowingly invalidating a running gate)
set -u -o pipefail
cd "$(dirname "$0")/.."
SPERRE="build/.gate.lock"

FORCE=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --force) FORCE=1 ;;
        *) ARGS+=("$a") ;;
    esac
done

if [ -e "$SPERRE" ] && [ "$FORCE" = "0" ]; then
    # flock -n succeeds only if nobody holds it. Held = a gate is running.
    if ! flock -n "$SPERRE" true 2>/dev/null; then
        echo "bau: a test gate is running — refusing to rebuild the binary under it." >&2
        echo "     Its late suites would measure a different build than its early ones, and the whole gate" >&2
        echo "     becomes worthless (measured 2026-09-22: five phantom failures, 17 minutes lost)." >&2
        echo "     Wait for it, or use --force if you accept invalidating that run." >&2
        exit 3
    fi
fi

set -- "${ARGS[@]+"${ARGS[@]}"}"
[ $# -eq 0 ] && set -- -j4
exec cmake --build build "$@"
