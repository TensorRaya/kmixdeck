#!/bin/bash
# D-Bus cost of a Levels signal: one signal per tick carrying a{sd} for N nodes, 25 Hz, measured at emitter + receiver + dbus-daemon
export XDG_RUNTIME_DIR=/run/user/$(id -u)
cat > /tmp/lvl_emit.py <<'PY'
import sys, time, os
from gi.repository import GLib, Gio
N=int(sys.argv[1]); HZ=int(sys.argv[2]); SECS=int(sys.argv[3])
bus=Gio.bus_get_sync(Gio.BusType.SESSION, None)
names=[f"kmixdeck.node.{i}" for i in range(N)]
def tick():
    v=GLib.Variant("(a{sd})", ({n: 0.0089 for n in names},))
    bus.emit_signal(None, "/org/kmixdeck1", "org.kmixdeck1.Levels", "Peaks", v)
    return True
GLib.timeout_add(1000//HZ, tick)
t0=time.process_time(); loop=GLib.MainLoop(); GLib.timeout_add_seconds(SECS, loop.quit); loop.run()
print(f"emitter cpu={time.process_time()-t0:.3f}s over {SECS}s = {100*(time.process_time()-t0)/SECS:.2f}%")
PY
cat > /tmp/lvl_recv.py <<'PY'
import sys, time
from gi.repository import GLib, Gio
SECS=int(sys.argv[1]); count=[0]
bus=Gio.bus_get_sync(Gio.BusType.SESSION, None)
def on(conn, sender, path, iface, sig, params): count[0]+=1
bus.signal_subscribe(None, "org.kmixdeck1.Levels", "Peaks", None, None, Gio.DBusSignalFlags.NONE, on)
t0=time.process_time(); loop=GLib.MainLoop(); GLib.timeout_add_seconds(SECS, loop.quit); loop.run()
print(f"receiver got {count[0]} signals, cpu={time.process_time()-t0:.3f}s = {100*(time.process_time()-t0)/SECS:.2f}%")
PY
python3 -c "import gi; gi.require_version('Gio','2.0')" 2>/dev/null || sudo -n apt-get install -y -qq python3-gi >/dev/null 2>&1
for N in 5 24; do
  echo "--- N=$N nodes, 25 Hz, 6 s, 1 emitter + 2 receivers, private bus"
  dbus-run-session -- bash -c '
    D=$(pgrep -n dbus-daemon); T0=$(awk "{print \$14+\$15}" /proc/$D/stat)
    /usr/bin/python3 /tmp/lvl_recv.py 7 & R1=$!; /usr/bin/python3 /tmp/lvl_recv.py 7 & R2=$!; sleep 0.5
    /usr/bin/python3 /tmp/lvl_emit.py '"$N"' 25 6
    wait $R1 $R2
    T1=$(awk "{print \$14+\$15}" /proc/$D/stat); echo "  dbus-daemon: $(( (T1-T0) ))/700 jiffies = $(/usr/bin/python3 -c "print(f\"{($T1-$T0)/7:.1f}%\")")"
  ' 2>&1 | sed 's/^/  /'
done
