// app.js — shell: connection, tabs, hearing bar, undo, re-render on patches. Views live in mixer.js / apps.js / patchbay.js.
import * as C from "./client.js";
import * as Mixer from "./mixer.js";
import * as Apps from "./apps.js";
import * as Patchbay from "./patchbay.js";
import * as Fx from "./fx.js";
import { el, toast, button } from "./widgets.js";

const VIEWS = { mixer: Mixer, apps: Apps, patchbay: Patchbay };
let view = location.hash.replace("#", "") in VIEWS ? location.hash.slice(1) : "mixer";
const main = document.getElementById("view");

function selectView(name) {
  view = name; history.replaceState(null, "", `#${name}`);
  for (const t of document.querySelectorAll("#tabs [role=tab]")) t.setAttribute("aria-selected", String(t.dataset.view === name));
  render();
}
document.getElementById("tabs").addEventListener("click", (ev) => { const t = ev.target.closest("[role=tab]"); if (t) selectView(t.dataset.view); });

let scheduled = false;
function render() {
  Fx.render();
  if (scheduled) return; scheduled = true;
  requestAnimationFrame(() => {
    scheduled = false;
    if (!C.state.connected) {
      main.replaceChildren(el("div", { class: "empty disconnected", probe: "disconnected" },
        el("h2", {}, "kmixdeck is not running"), el("p", {}, "The bridge is up, but the daemon is not on the bus. Start kmixdeck on the desk machine and this page picks it up by itself.")));
    } else VIEWS[view].render(main);
    hearing(); undo();
  });
}

// UX-2 "what am I hearing" — device + the mixes that reach it (same data the tray shows)
function hearing() {
  const bar = document.getElementById("hearing"), dev = C.state.root.ListeningDevice;
  bar.replaceChildren();
  if (!C.state.connected) return;
  const desc = (C.state.root.OutputDevices || {})[dev] || dev || "no listening device";
  const on = C.mixes().filter((m) => m.Outputs?.includes(dev));
  bar.append(el("span", { class: "hear-label" + (dev && on.length ? "" : " none"), probe: "hearLabel" }, "🎧 ", desc, on.length ? ` ← ${on.map((m) => m.Name).join(", ")}` : dev ? " ← nothing" : ""));
  const devs = Object.entries(C.state.root.OutputDevices || {});
  if (devs.length) {
    const sel = el("select", { probe: "listeningDeviceBox", "aria-label": "Listening device", onchange: (ev) => C.set(C.ROOT, "ListeningDevice", ev.target.value).catch((e) => toast(e.message, true)) });
    for (const [node, d] of devs) sel.append(el("option", { value: node, selected: node === dev }, d || node));
    bar.append(sel);
  }
}

function undo() {
  const b = document.getElementById("undo"), d = C.state.root.UndoDescription;
  b.hidden = !d; if (d) { b.textContent = `↶ ${d}`; b.onclick = () => C.call(C.ROOT, "Undo").catch((e) => toast(e.message, true)); }
}

const conn = document.getElementById("conn");
C.onChange((patch) => {
  conn.textContent = C.state.connected ? "" : "⚠ offline";
  conn.dataset.state = C.state.connected ? "online" : "offline";
  if (patch && patch.closeCode === 4001) { main.replaceChildren(el("div", { class: "empty" }, el("h2", {}, "Wrong token"), el("p", {}, "Open the link kmixdeck printed (it carries the token), or scan its QR code."))); return; }
  render();
});
C.connect();
render();

// keyboard: 1/2/3 switch views (UX-4); the faders handle their own keys
document.addEventListener("keydown", (ev) => {
  if (ev.target.matches("input, select, textarea, [role=slider]")) return;
  const k = { 1: "mixer", 2: "apps", 3: "patchbay" }[ev.key]; if (k) selectView(k);
  if (ev.key === "z" && (ev.ctrlKey || ev.metaKey)) { ev.preventDefault(); C.call(C.ROOT, "Undo").catch(() => {}); }
});

// test hook (the same idea as the KDE window's --probe): window.kmixdeck.probe("cellFader/game/stream").dataset.value
window.kmixdeck = { state: C.state, peaks: C.peaks, probe: (name) => document.querySelector(`[data-probe="${name}"]`), probes: () => [...document.querySelectorAll("[data-probe]")].map((e) => e.dataset.probe), selectView, C };
