#!/usr/bin/env python3
"""ADR 0013 measurement: the dv30c desk (32 mono channels + 4 stereo mixes on a 32x32 virtual device), built through
the CLI exactly like test_dv30c, against a private PipeWire with the DEFAULT open-files limit (no drop-in).
Prints one JSON line: nodes, pipewire server fds, clients, links, build time, missing cells, LastError, and from the
daemon log the count of PipeWire core errors, MX-2 restores and "never appeared" re-requests.

  tools/measure-dv30c.py [RUN]                   one run, quiet machine
  tools/measure-dv30c.py --load 3 --runs 10      ten runs under 3 nice-19 busy loops (the ADR 0013 series F1-F10)

Needs the build in ./build (TMPDIR on a disk with room: the sandbox copies the PipeWire config tree)."""
import sys, os, time, json, subprocess, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tests", "integration"))
from pw_sandbox import start_private_pipewire
from test_service_cli import Stack

def fds(pid):
    try: return len(os.listdir(f"/proc/{pid}/fd"))
    except Exception: return -1

def one(run, outdir):
    d = start_private_pipewire(); d.wait_node("kmixdeck.mix.stream")
    s = Stack(d)
    try:
        pwpid = next(p.pid for p in d.procs if "wireplumber" not in " ".join(p.args if isinstance(p.args, list) else [p.args]))
        soft = open(f"/proc/{pwpid}/limits").read().split("Max open files")[1].split()[0]
        node = s.cli("devices", "virtual", "add", "Desk", "--in", "32", "--out", "32").stdout.strip()
        din, dout = node, node + ".out"
        d.wait_node(dout); d.wait_node(din, timeout=15); d.wait_ports(din, 32); d.wait_ports(dout, 32)
        t0 = time.time()
        for i in range(1, 33):
            s.cli("channel", "add", f"d{i}"); s.cli("channel", "input", f"d{i}", f"{din}:AUX{i}")
        for k in range(4):
            s.cli("mix", "add", f"r{k}"); s.cli("mix", "output", f"r{k}", f"{dout}:AUX{2*k+1},AUX{2*k+2}")
        built = time.time() - t0
        want = [f"kmixdeck.in.d{i}.in" for i in range(1, 33)] + [f"kmixdeck.out.r{k}" for k in range(4)] + \
               [f"kmixdeck.cells.d{i}" for i in range(1, 33)] + [f"kmixdeck.tap.r{k}" for k in range(4)]
        t1 = time.time(); d.wait_nodes(want, timeout=180); visible = time.time() - t1
        time.sleep(2.0)
        # every cell of every channel carries all 6 mixes (monitor, stream, r0..r3)
        missing = [(i, m) for i in range(1, 33) for m in ("monitor", "stream", "r0", "r1", "r2", "r3") if m not in d.cell_gains(f"d{i}")]
        dump = d.dump()
        nodes = [o for o in dump if o.get("type", "").endswith("Node")]
        km = [o for o in nodes if str(o["info"]["props"].get("node.name", "")).startswith("kmixdeck.")]
        clients = [o for o in dump if o.get("type", "").endswith("Client")]
        links = [o for o in dump if o.get("type", "").endswith("Link")]
        # modules the daemon loads live in ITS process (pw_context_load_module) — count what they create instead
        nm = [str(o["info"]["props"].get("node.name", "")) for o in km]
        chains = [n for n in nm if n.startswith("kmixdeck.cells.") and not n.endswith(".out")]
        loopbacks = [n for n in nm if n.endswith(".in") or n.startswith("kmixdeck.link.")]   # capture side of every loopback
        st = s.cli("status", json_out=True)
        err = st.get("LastError", st.get("lastError", ""))
        # audio: one cell at 0.25 must show -12 dB only in its mix
        s.cli("cell", "set", "d1", "r0", "0.25"); time.sleep(0.5)
        r = dict(run=run, nofile_soft=soft, build_s=round(built, 1), visible_s=round(visible, 2), nodes=len(nodes), kmixdeck_nodes=len(km),
                 server_fds=fds(pwpid), daemon_fds=fds(s.daemon.pid), clients=len(clients), links=len(links),
                 loopback_modules=len(loopbacks), filter_chains=len(chains), cells_missing=len(missing), last_error=err,
                 cell_d1_r0=round(d.cell_gain("d1", "r0"), 4), cell_d1_r1=round(d.cell_gain("d1", "r1"), 4))
        lg = open(s.daemon_log_path).read()
        r.update(core_errors=lg.count("core error"), mx2_restores=lg.count("behind our back"), never_appeared=lg.count("never appeared"))
        print(json.dumps(r)); sys.stdout.flush()
    except Exception:
        import shutil, traceback
        tag = os.path.join(outdir, f"fail.{run}")
        shutil.copy(s.daemon_log_path, tag + ".daemon.log")
        open(tag + ".nodes", "w").write("\n".join(sorted(str(o["info"]["props"].get("node.name")) for o in d.dump() if o.get("type","").endswith("Node"))))
        open(tag + ".status.json", "w").write(s.cli("--json", "status", check=False).stdout + "\n" + s.cli("--json", "mix", "list", check=False).stdout)
        traceback.print_exc(); raise
    finally:
        s.close(); d.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run", nargs="?", default="0")
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--load", type=int, default=0, help="number of nice-19 busy loops during the runs")
    ap.add_argument("--out", default=".", help="where fail.<run>.* diagnostics go")
    a = ap.parse_args()
    hogs = [subprocess.Popen(["nice", "-n", "19", "sh", "-c", "while :; do :; done"]) for _ in range(a.load)]
    failed = 0
    try:
        for i in range(1, a.runs + 1):
            run = a.run if a.runs == 1 else f"{a.run}{i}"
            try: one(run, a.out)
            except Exception: failed += 1
    finally:
        for h in hogs: h.kill()
    print(f"# {failed} failed of {a.runs}", file=sys.stderr)
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
