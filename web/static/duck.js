// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
// duck.js — FX-9: turn one channel down while another one talks. Same four values as `kmixdeck duck set` and
// DuckPanel.qml: {duckedBy, depth, threshold, attack, release}. "How hard" is depth; the daemon watches the
// trigger channel's meter and rides the gain, so nothing is patched here.
import * as C from "./client.js";
import { el, button, iconButton, toast } from "./widgets.js";

let open = null;   // { path, title }

// The ranges are the daemon's (Mixer::setDucking) — a slider that can produce a refused value is a broken slider.
export const GRENZEN = {
  depth:     { min: -60, max: 0,   step: 1, label: "Depth",     unit: "dB",   hint: "How far the channel drops while the trigger is active" },
  threshold: { min: -60, max: 0,   step: 1, label: "Threshold", unit: "dBFS", hint: "How loud the trigger has to be before ducking starts" },
  attack:    { min: 2,   max: 400, step: 1, label: "Attack",    unit: "ms",   hint: "How fast it ducks once the trigger opens" },
  // step must land on round numbers: a range input snaps to min + n*step, so min 2 with step 5 can only ever
  // produce 2, 7, 12 … 502 — asking for 500 ms silently gave 502 (measured 2026-09-21). Hence step 1 here.
  release:   { min: 2,   max: 800, step: 1, label: "Release",   unit: "ms",   hint: "How long it takes to come back up" },
};
export const STANDARD = { duckedBy: "", depth: -12, threshold: -40, attack: 10, release: 200 };

export function duckingOf(obj) {
  try { const d = JSON.parse(obj.Ducking || ""); return d && typeof d === "object" ? { ...STANDARD, ...d } : { ...STANDARD }; }
  catch { return { ...STANDARD }; }
}
export function ducked(obj) { return !!duckingOf(obj).duckedBy; }

// FX-9-Badge am Kanal: WER duckt und, solange es wirklich absenkt, WIE VIEL. Die Spec verlangt beides.
function triggerName(slug) {
  const ch = C.channels().find((c) => c.Slug === slug);
  return ch ? ch.Name : slug;
}
export function duckBadge(obj) {
  const wer = triggerName(duckingOf(obj).duckedBy);
  const jetzt = Number(obj.DuckReduction || 0);
  return jetzt < -0.1 ? `\u2193 ${wer} ${jetzt.toFixed(1)} dB` : `\u2193 ${wer}`;
}
export function duckTooltip(obj) {
  return `Ducked by ${triggerName(duckingOf(obj).duckedBy)} — turns down while that channel carries signal`;
}

export function openDuck(obj, title) { open = { path: obj.path, title }; render(); }
export function closeDuck() { open = null; render(); }

async function save(d) {
  if (!C.state.objects[open.path]) return;
  // A method, not a property write: a refused value has to come back with its reason, and a D-Bus property
  // setter cannot answer an error (see rejectProperty in service.h). This is what surfaces in the toast.
  await C.call(open.path, "SetDucking", JSON.stringify(d)).catch((e) => toast(e.message, true));
}

export function render() {
  const host = document.getElementById("duck-drawer");
  if (!host) return;
  host.innerHTML = "";
  if (!open) { host.hidden = true; document.body.classList.remove("duck-open"); return; }
  const obj = C.state.objects[open.path] && { path: open.path, ...C.state.objects[open.path] };
  if (!obj) { open = null; host.hidden = true; document.body.classList.remove("duck-open"); return; }
  host.hidden = false; document.body.classList.add("duck-open");
  const d = duckingOf(obj);
  host.dataset.probe = "duckPanel"; host.dataset.value = obj.path;

  host.append(el("div", { class: "fx-head" },
    el("h2", {}, `Ducking on ${open.title}`),
    iconButton("close", { title: "Close", cls: "menu", probe: "duckClose", onClick: closeDuck })));

  // Which channel triggers it. Itself is not offered — the daemon refuses it, so the UI should not suggest it.
  const sel = el("select", { probe: "duckBy", "aria-label": "Channel that triggers the ducking" });
  sel.append(el("option", { value: "" }, "Not ducked"));
  for (const ch of C.channels()) {          // C.channels() → [{path, ...props}], not [path, props] pairs
    if (ch.path === obj.path) continue;
    const o = el("option", { value: ch.Slug }, ch.Name || ch.Slug);
    if (ch.Slug === d.duckedBy) o.selected = true;
    sel.append(o);
  }
  sel.dataset.value = d.duckedBy;
  sel.onchange = () => save({ ...d, duckedBy: sel.value });
  host.append(el("div", { class: "fx-param" }, el("label", {}, "Triggered by"), sel));

  // The live reduction, so "how hard" is visible while it happens rather than only configured.
  const jetzt = el("output", { probe: "duckReduction" }, `${(+obj.DuckReduction || 0).toFixed(1)} dB`);
  jetzt.dataset.value = String(+obj.DuckReduction || 0);
  host.append(el("div", { class: "fx-param" }, el("label", {}, "Right now"), jetzt));

  const list = el("div", { class: "fx-list", probe: "duckParams" });
  list.dataset.value = String(Object.keys(GRENZEN).length);
  for (const [key, g] of Object.entries(GRENZEN)) {
    const out = el("output", {}, `${d[key]} ${g.unit}`);
    const slider = Object.assign(
      el("input", { type: "range", min: g.min, max: g.max, step: g.step, probe: `duckParam/${key}`, title: g.hint, "aria-label": `${g.label} in ${g.unit}` }),
      { value: d[key], disabled: !d.duckedBy });
    slider.dataset.value = String(d[key]);
    slider.oninput = () => { out.textContent = `${slider.value} ${g.unit}`; slider.dataset.value = slider.value; };
    slider.onchange = () => save({ ...d, [key]: +slider.value });
    list.append(el("div", { class: "fx-param" }, el("label", { title: g.hint }, `${g.label} (${g.unit})`), slider, out));
  }
  host.append(list);

  if (d.duckedBy)
    host.append(el("div", { class: "fx-presets" },
      button("Stop ducking", { cls: "danger", probe: "duckClear", title: "Remove the ducker from the graph", onClick: () => save({}) })));
}
