#!/bin/bash
# Regenerate prototype/kmixdeck-prototype.conf from the starter layout via the daemon, so the two never drift.
cd ~/repos/kmixdeck; export XDG_RUNTIME_DIR=/run/user/$(id -u)
timeout 400 ninja -C build > /tmp/build.log 2>&1; echo "build rc=$?"; grep -E "error:" /tmp/build.log | head -3
RT=$(mktemp -d); XDG_CONFIG_HOME=$RT dbus-run-session -- sh -c './build/bin/kmixdeckd >/dev/null 2>&1 & sleep 2.5; kill %1' 2>/dev/null
test -f $RT/pipewire/pipewire.conf.d/90-kmixdeck.conf && { { echo "# kmixdeck reference graph — GENERATED from the starter layout by kmixdeckd (see src/layout.cpp)."; echo "# Kept in the repo as the readable reference for ADR 0002 and as the sandbox default for tests."; echo "# Regenerate: tests/tools/regen-prototype.sh"; tail -n +2 $RT/pipewire/pipewire.conf.d/90-kmixdeck.conf; } > prototype/kmixdeck-prototype.conf; echo "regenerated: $(wc -l < prototype/kmixdeck-prototype.conf) lines"; }
mkdir -p tests/tools; cp /tmp/kmix-regen.sh tests/tools/regen-prototype.sh 2>/dev/null
cp prototype/kmixdeck-prototype.conf ~/.config/pipewire/pipewire.conf.d/90-kmixdeck-prototype.conf; systemctl --user restart pipewire wireplumber
timeout 400 python3 -m pytest -q tests/integration > /tmp/pytest.log 2>&1; echo "pytest rc=$?"; grep -E "passed|failed|FAILED" /tmp/pytest.log | head -5
