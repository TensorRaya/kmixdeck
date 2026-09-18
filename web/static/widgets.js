// widgets.js — fader, meter, small buttons. DOM only, no framework (ADR 0011 §4).
import { peaks, dbLabel, cubicToLin, linToCubic } from "./client.js";

const FLOOR_DB = -60;
const fracOf = (lin) => (lin <= 0 ? 0 : Math.max(0, Math.min(1, 1 - (20 * Math.log10(lin)) / FLOOR_DB)));

/** Vertical peak meter, same drawing rules as LevelMeter.qml (UX-6/UX-16): dB scale −60…0, colour bands with 2 dB
 *  hysteresis, peak-hold marker that sits 1 s and then slides. Ballistics (hold/fall) come from the daemon. */
export function meter(key, { horizontal = false, probe } = {}) {
  const el = document.createElement("div");
  el.className = "meter" + (horizontal ? " horizontal" : "");
  if (probe) el.dataset.probe = probe;
  el.dataset.meterKey = key;
  el.innerHTML = '<div class="bar"></div><div class="hold"></div><div class="clip"></div>';
  const bar = el.firstChild, hold = bar.nextSibling;
  let band = 0, holdFrac = 0, holdUntil = 0;
  el._tick = (now) => {
    const lin = peaks[el.dataset.meterKey] ?? 0, frac = fracOf(lin), db = lin > 0 ? 20 * Math.log10(lin) : -Infinity;
    if (band === 0 && db > -18) band = db > -6 ? 2 : 1;
    else if (band === 1) { if (db > -6) band = 2; else if (db < -20) band = 0; }
    else if (band === 2 && db < -8) band = 1;
    el.dataset.band = band;
    if (frac >= holdFrac) { holdFrac = frac; holdUntil = now + 1000; }
    else if (now > holdUntil) holdFrac = Math.max(frac, holdFrac - 0.012);
    bar.style.transform = horizontal ? `scaleX(${frac})` : `scaleY(${frac})`;
    hold.style.display = holdFrac > 0.02 ? "" : "none";
    hold.style[horizontal ? "left" : "bottom"] = `${holdFrac * 100}%`;
    el.dataset.level = frac.toFixed(3);
  };
  meters.add(el);
  return el;
}
const meters = new Set();
function frame(now) {
  for (const m of meters) { if (!m.isConnected) { meters.delete(m); continue; } m._tick(now); }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

/** Fader: pointer-events slider with a cubic curve (UX-7), keyboard (UX-4), ≥ 44 px hit area on touch.
 *  `value` is linear gain 0…1 (what the bus speaks); the handle position is cubic. onInput fires while dragging,
 *  onCommit once on release / key. Double-tap → unity (1.0). */
export function fader({ value = 1, max = 1, vertical = true, label, probe, onInput, onCommit }) {
  const el = document.createElement("div");
  el.className = "fader" + (vertical ? " vertical" : " horizontal");
  el.setAttribute("role", "slider"); el.tabIndex = 0;
  el.setAttribute("aria-valuemin", "0"); el.setAttribute("aria-valuemax", "100");
  if (label) el.setAttribute("aria-label", label);
  if (probe) el.dataset.probe = probe;
  el.innerHTML = '<div class="track"><div class="fill"></div></div><div class="handle"></div><output class="db"></output>';
  const fill = el.querySelector(".fill"), handle = el.querySelector(".handle"), out = el.querySelector(".db");
  const maxCubic = linToCubic(max);
  let lin = value, dragging = false, lastTap = 0;

  const render = () => {
    const pos = Math.min(1, linToCubic(lin) / maxCubic);
    fill.style.transform = vertical ? `scaleY(${pos})` : `scaleX(${pos})`;
    el.style.setProperty("--pos", pos);                         // handle travels inside the track's padded range (CSS)
    out.textContent = dbLabel(lin);
    el.setAttribute("aria-valuenow", Math.round(pos * 100));
    el.setAttribute("aria-valuetext", `${dbLabel(lin)}, ${Math.round(lin * 100)} %`);
    el.dataset.value = lin.toFixed(4);
  };
  const setLin = (v, commit) => { lin = Math.max(0, Math.min(max, v)); render(); (commit ? onCommit : onInput)?.(lin); };
  const posFromEvent = (ev) => {
    const r = el.getBoundingClientRect();
    return vertical ? 1 - (ev.clientY - r.top) / r.height : (ev.clientX - r.left) / r.width;
  };
  el.addEventListener("pointerdown", (ev) => {
    if (ev.button !== 0) return;
    const now = performance.now();
    if (now - lastTap < 350) { lastTap = 0; setLin(1, true); return; }
    lastTap = now; dragging = true; el.setPointerCapture(ev.pointerId); el.classList.add("dragging");
    setLin(cubicToLin(Math.max(0, Math.min(1, posFromEvent(ev))) * maxCubic), false); ev.preventDefault();
  });
  el.addEventListener("pointermove", (ev) => { if (dragging) setLin(cubicToLin(Math.max(0, Math.min(1, posFromEvent(ev))) * maxCubic), false); });
  const up = (ev) => { if (!dragging) return; dragging = false; el.classList.remove("dragging"); setLin(lin, true); };
  el.addEventListener("pointerup", up); el.addEventListener("pointercancel", up);
  el.addEventListener("wheel", (ev) => { ev.preventDefault(); setLin(cubicToLin(Math.max(0, linToCubic(lin) - Math.sign(ev.deltaY) * 0.02)), true); }, { passive: false });
  el.addEventListener("keydown", (ev) => {
    const step = ev.shiftKey ? 0.05 : 0.01, c = linToCubic(lin);
    const k = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: 0.1, PageDown: -0.1 }[ev.key];
    if (k !== undefined) setLin(cubicToLin(Math.max(0, Math.min(maxCubic, c + k))), true);
    else if (ev.key === "Home") setLin(0, true);
    else if (ev.key === "End") setLin(max, true);
    else if (ev.key === "0") setLin(1, true);
    else return;
    ev.preventDefault();
  });
  el.update = (v) => { if (!dragging) { lin = v; render(); } };
  render();
  return el;
}

export function button(text, { probe, cls = "", pressed, title, onClick } = {}) {
  const b = document.createElement("button");
  b.type = "button"; b.className = cls; b.textContent = text;
  if (probe) b.dataset.probe = probe;
  if (title) b.title = title;
  if (pressed !== undefined) b.setAttribute("aria-pressed", String(pressed));
  b.addEventListener("click", (ev) => { ev.stopPropagation(); onClick?.(ev); });
  b.update = (p) => b.setAttribute("aria-pressed", String(p));
  return b;
}

export function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v; else if (k === "probe") e.dataset.probe = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === "style" && typeof v === "object") for (const [prop, val] of Object.entries(v)) prop.startsWith("--") ? e.style.setProperty(prop, val) : (e.style[prop] = val);
    else if (v !== undefined && v !== null && v !== false) e.setAttribute(k, v === true ? "" : v);
  }
  for (const c of children.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(c));
  return e;
}

export function toast(msg, isError = false) {
  const t = document.getElementById("toast"); t.textContent = msg; t.classList.toggle("error", isError); t.classList.add("show");
  clearTimeout(t._h); t._h = setTimeout(() => t.classList.remove("show"), 2500);
}
