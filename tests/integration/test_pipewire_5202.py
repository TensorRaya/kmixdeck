# SPDX-License-Identifier: GPL-3.0-or-later
"""DV-34 / ADR 0015: PipeWire #5202 must not crash kmixdeckd or the PipeWire server.

PipeWire 1.6.0-1.6.2 ship a filter-chain module that reads freed memory while it connects its streams. Without help
the freed bytes are usually still intact, so the bug shows up now and then as garbage in `pw-dump` (CI runs
37378915814, 37391578286). tests/tools/realloc-guard.c makes it deterministic: preloaded into ONE process, every
page-multiple realloc moves the block and leaves the old pages PROT_NONE, so the stale read faults.

What is checked, with the probe preloaded into the process under test (for the server test that is the sandbox's
pipewire AND wireplumber, which it starts with one environment):
  - the probe itself: it fires on a stale read and leaves a normal program alone (otherwise nothing below proves much);
  - kmixdeckd builds cell chains (add a mix, add a channel, set a cell) and survives, mapping the module that
    pw::filterChainModule() names;
  - the PipeWire server loads the login fragment for 4 channels x 3 mixes and survives, mapping the same module;
  - a fragment naming kmixdeck's module while the file is missing still lets PipeWire start (`nofail`).

Seen red before the fix (2026-10-06, PipeWire 1.6.2, stock module): the daemon died 5/5 and the server 5/5.
Red check, any time: KMIXDECK_TEST_STOCK_FILTER_CHAIN=1 hides kmixdeck's module from the module path, so the daemon
falls back to PipeWire's own (and warns) — on an affected PipeWire the daemon and server tests must then fail.
On a PipeWire without the bug the file only checks that nothing changed: the stock module is used.
"""
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

import pytest

from pw_sandbox import start_private_pipewire
from test_service_cli import BIN, Stack
from waiting import wait_for

FIXED = "libpipewire-module-kmixdeck-filter-chain"
STOCK = "libpipewire-module-filter-chain"
FIXED_DIR = BIN.parent / "lib" / "pipewire-0.3"   # the build puts our module here (third_party/pipewire)
GUARD = BIN / "realloc-guard.so"
SELFTEST = BIN / "realloc-guard-selftest"


def pipewire_version() -> tuple[int, ...]:
    out = subprocess.run(["pkg-config", "--modversion", "libpipewire-0.3"], capture_output=True, text=True, check=True).stdout
    return tuple(int(x) for x in out.strip().split(".")[:3])


AFFECTED = (1, 6, 0) <= pipewire_version() < (1, 6, 3)


def stock_module_dir() -> Path:
    return Path(subprocess.run(["pkg-config", "--variable=moduledir", "libpipewire-0.3"],
                               capture_output=True, text=True, check=True).stdout.strip())


RED_CHECK = os.environ.get("KMIXDECK_TEST_STOCK_FILTER_CHAIN") == "1"


def module_path() -> str:
    """PIPEWIRE_MODULE_DIR as an install would see it: kmixdeck's module next to PipeWire's own."""
    return f"{FIXED_DIR}:{stock_module_dir()}" if AFFECTED and not RED_CHECK else str(stock_module_dir())


def mapped_filter_chains(pid: int) -> set[str]:
    maps = Path(f"/proc/{pid}/maps").read_text()
    return {Path(line.split()[-1]).name for line in maps.splitlines() if "filter-chain" in line and line.split()[-1].startswith("/")}


def expected_module() -> str:
    """What the daemon must pick here. In the red check that is PipeWire's own module — the tests must then fail
    because a process dies, not because a name differs."""
    return FIXED if AFFECTED and not RED_CHECK else STOCK


@pytest.fixture(scope="module", autouse=True)
def built():
    assert GUARD.exists() and SELFTEST.exists(), "build the tests first (tests/CMakeLists.txt builds the probe)"
    if AFFECTED:
        assert (FIXED_DIR / f"{FIXED}.so").exists(), f"PipeWire {pipewire_version()} has #5202 but the fixed module was not built"


def test_dv34_the_probe_turns_a_stale_read_into_a_crash_and_nothing_else():
    """Rule Zero for this file: a probe that has not been seen firing proves nothing."""
    plain = subprocess.run([str(SELFTEST)], capture_output=True, text=True, timeout=10)
    assert plain.returncode == 0 and "stale read=x" in plain.stdout, plain   # without it the bug is invisible
    probed = subprocess.run([str(SELFTEST)], capture_output=True, text=True, timeout=10, env=dict(os.environ, LD_PRELOAD=str(GUARD)))
    assert probed.returncode == -11 and "moved=1 copy=x" in probed.stdout, probed
    # and a normal program under the probe behaves normally
    ok = subprocess.run(["pw-cli", "--version"], capture_output=True, text=True, timeout=10, env=dict(os.environ, LD_PRELOAD=str(GUARD)))
    assert ok.returncode == 0, ok


def test_dv34_kmixdeckd_builds_cell_chains_with_the_probe_and_survives():
    pw = start_private_pipewire()
    try:
        pw.wait_node("kmixdeck.mix.stream")
        st = Stack(dict_env(pw, PIPEWIRE_MODULE_DIR=module_path()))
        # the probe goes into kmixdeckd only — not into the CLI, the bus or PipeWire
        st.daemon.terminate(); st.daemon.wait(timeout=5)
        st.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=dict(st.env, LD_PRELOAD=str(GUARD)),
                                     stdout=subprocess.DEVNULL, stderr=open(st.daemon_log_path, "a"))
        try:
            wait_for(lambda: st.cli("status", check=False).returncode == 0, timeout=10, what="kmixdeckd up under the probe")
            for step in (["mix", "add", "Aufnahme"], ["channel", "add", "Mikrofon"], ["cell", "set", "mikrofon", "aufnahme", "-6dB"]):
                r = st.cli(*step, check=False)
                assert st.daemon.poll() is None, (f"kmixdeckd died (rc={st.daemon.returncode}) after `kmixdeck {' '.join(step)}`\n"
                                                  + Path(st.daemon_log_path).read_text()[-1500:])
                assert r.returncode == 0, r
            pw.wait_node("kmixdeck.cells.mikrofon", timeout=10)
            time.sleep(2.0)   # the chains of every channel are rebuilt for the new mix; give them time to connect
            assert st.daemon.poll() is None, f"kmixdeckd died: rc={st.daemon.returncode}\n" + Path(st.daemon_log_path).read_text()[-2000:]
            assert "realloc-guard.so" in Path(f"/proc/{st.daemon.pid}/maps").read_text(), "the probe is not in kmixdeckd"
            assert mapped_filter_chains(st.daemon.pid) == {f"{expected_module()}.so"}
            # what the daemon loaded is what it wrote for the next login
            conf = (Path(pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
            other = STOCK if AFFECTED else FIXED
            assert f"name = {expected_module()} args" in conf and f"name = {other} args" not in conf
        finally:
            st.close()
    finally:
        pw.close()


def fragment_for_four_by_three(pw) -> str:
    """The login fragment kmixdeckd writes for 4 channels x 3 mixes (big enough to hit #5202 at server start)."""
    st = Stack(dict_env(pw, PIPEWIRE_MODULE_DIR=module_path()))
    try:
        st.cli("mix", "add", "Aufnahme"); st.cli("channel", "add", "Mikrofon")
        conf = Path(pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf"
        wait_for(lambda: "kmixdeck.cells.mikrofon" in conf.read_text() and "Laufnahme" in conf.read_text(),
                 timeout=10, what="fragment with the 4th channel and the 3rd mix")
        return conf.read_text()
    finally:
        st.close()


class dict_env:
    """A PwDaemon stand-in for Stack(): same runtime dir, extra environment."""
    def __init__(self, pw, **extra):
        self.runtime_dir, self.env = pw.runtime_dir, dict(pw.env, **extra)


def test_dv34_the_server_loads_the_login_fragment_with_the_probe_and_survives():
    pw = start_private_pipewire()
    try:
        pw.wait_node("kmixdeck.mix.stream")
        conf = fragment_for_four_by_three(pw)
        assert conf.count(f"name = {expected_module()} args") >= 4   # one cell chain per channel
        pw.env.update(PIPEWIRE_MODULE_DIR=module_path(), LD_PRELOAD=str(GUARD))
        try:
            pw.restart(wait_for="kmixdeck.cells.mikrofon")
        except (RuntimeError, AssertionError, subprocess.CalledProcessError) as e:   # a dead server also fails pw-dump
            rc = pw.procs[0].poll() if pw.procs else None
            pytest.fail(f"PipeWire did not come up with the login fragment under the probe (server rc={rc}): {e}")
        finally:
            pw.env.pop("LD_PRELOAD")
        server = pw.procs[0]
        time.sleep(1.0)
        assert server.poll() is None, f"PipeWire server died loading the fragment: rc={server.returncode}"
        assert "realloc-guard.so" in Path(f"/proc/{server.pid}/maps").read_text(), "the probe is not in the server"
        assert mapped_filter_chains(server.pid) == {f"{expected_module()}.so"}
        names = pw.node_names()
        assert {f"kmixdeck.cells.{c}" for c in ("game", "system", "voice", "mikrofon")} <= names
    finally:
        pw.close()


@pytest.mark.skipif(not AFFECTED or RED_CHECK, reason="the fragment names kmixdeck's module only on PipeWire 1.6.0-1.6.2")
def test_dv34_a_missing_fixed_module_does_not_stop_pipewire():
    """kmixdeck uninstalled, fragment left behind: PipeWire must still start (and the session keep its sound)."""
    pw = start_private_pipewire()
    try:
        pw.wait_node("kmixdeck.mix.stream")
        conf = fragment_for_four_by_three(pw)
        assert conf.count("flags = [ nofail ]") == conf.count(f"name = {FIXED} args") > 0
        pw.env["PIPEWIRE_MODULE_DIR"] = str(stock_module_dir())   # our module is not there
        pw.restart(wait_for="kmixdeck.mix.stream")
        assert pw.procs[0].poll() is None
        assert "kmixdeck.cells.game" not in pw.node_names()   # the chains are missing, nothing else is
    finally:
        pw.close()


def test_dv34_the_suite_ran_against_the_module_it_claims():
    """Guard against a green run that never exercised the fix: on an affected PipeWire the build must ship the module,
    and its only export must be the module entry point."""
    if not AFFECTED:
        assert not (FIXED_DIR / f"{FIXED}.so").exists(), "the fixed module was built for a PipeWire without #5202"
        return
    nm = subprocess.run(["nm", "-D", "--defined-only", str(FIXED_DIR / f"{FIXED}.so")], capture_output=True, text=True, check=True).stdout
    exported = sorted(line.split()[-1] for line in nm.splitlines() if re.match(r"^[0-9a-f]+ T ", line))
    assert exported == ["pipewire__module_init"], exported
    assert shutil.which("pw-cli")
