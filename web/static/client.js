// client.js — the WebSocket side of the web UI (ADR 0011). One connection, one state object, RFC 7386 patches.
// Nothing in here knows what a channel is; it knows paths, properties and methods — the same the KDE window uses.

export const state = { connected: false, root: {}, objects: {} };
export const peaks = {};                       // meter key → linear peak, overwritten 25×/s; read by requestAnimationFrame
const listeners = new Set();
let ws = null, nextId = 1, pending = new Map(), backoff = 500;

export function onChange(fn) { listeners.add(fn); return () => listeners.delete(fn); }
function notify(patch) { for (const fn of listeners) fn(patch); }

/** RFC 7386: objects merge recursively, null deletes, everything else replaces. */
export function mergePatch(target, patch) {
  for (const [k, v] of Object.entries(patch)) {
    if (v === null) delete target[k];
    else if (v && typeof v === "object" && !Array.isArray(v)) {
      if (!target[k] || typeof target[k] !== "object" || Array.isArray(target[k])) target[k] = {};
      mergePatch(target[k], v);
    } else target[k] = v;
  }
  return target;
}

export function token() {
  const m = location.hash.match(/token=([^&]+)/);
  if (m) { sessionStorage.setItem("kmixdeck-token", m[1]); history.replaceState(null, "", location.pathname); }
  return sessionStorage.getItem("kmixdeck-token") || "";
}

export function connect() {
  ws = new WebSocket(`${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws`);
  ws.onopen = () => { ws.send(JSON.stringify({ op: "hello", token: token() })); backoff = 500; };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    switch (m.op) {
      case "snapshot": Object.assign(state, { root: {}, objects: {} }, m.state); notify(null); break;
      case "patch":    mergePatch(state, m.state); notify(m.state); break;
      case "meters":   Object.assign(peaks, m.peaks); break;
      case "result": case "error": {
        const p = pending.get(m.id); pending.delete(m.id);
        if (p) m.op === "result" ? p.resolve(m.value) : p.reject(new Error(m.message));
        break;
      }
    }
  };
  ws.onclose = (ev) => {
    state.connected = false; notify({ connected: false, closeCode: ev.code });
    if (ev.code === 4001) return;                      // bad token — reconnecting will not help
    setTimeout(connect, backoff); backoff = Math.min(backoff * 2, 8000);
  };
}

function send(msg) {
  return new Promise((resolve, reject) => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return reject(new Error("not connected"));
    const id = nextId++; pending.set(id, { resolve, reject }); ws.send(JSON.stringify({ ...msg, id }));
  });
}
export const set  = (path, property, value) => send({ op: "set", path, property, value });
export const call = (path, method, ...args) => send({ op: "call", path, method, args });

// ---- view-model helpers: the shape the KDE window's MixerClient.overview() has (AR-9), derived, never stored
export const ROOT = "/org/kmixdeck1";
export const byIface = (iface) => Object.entries(state.objects).filter(([, o]) => o.interface === `org.kmixdeck1.${iface}`)
                                          .map(([path, o]) => ({ path, ...o }));
export function channels() {
  const order = state.root.ChannelOrder || [];
  return byIface("Channel").sort((a, b) => order.indexOf(a.Slug) - order.indexOf(b.Slug));
}
export function mixes() {
  const order = state.root.MixOrder || [];
  return byIface("Mix").sort((a, b) => order.indexOf(a.Slug) - order.indexOf(b.Slug));
}
export const cell = (chPath, mixPath) => byIface("Cell").find((c) => c.Channel === chPath && c.Mix === mixPath);
export const apps = () => byIface("App").sort((a, b) => a.Name.localeCompare(b.Name));
export const slugOf = (path) => path.split("/").pop();
export const meterKey = { channel: (s) => `channel/${s}`, mix: (s) => `mix/${s}`, cell: (c, m) => `cell/${c}/${m}`, app: (id) => `app/${id}` };

// ---- dB helpers (UX-7): sliders are cubic like the window; labels are dB and percent
export const linToDb = (v) => (v <= 0 ? -Infinity : 20 * Math.log10(v));
export const dbLabel = (v) => (v <= 0.0001 ? "−∞" : `${linToDb(v) >= 0 ? "+" : ""}${linToDb(v).toFixed(1)} dB`);
export const cubicToLin = (c) => c * c * c;
export const linToCubic = (l) => Math.cbrt(Math.max(0, l));
