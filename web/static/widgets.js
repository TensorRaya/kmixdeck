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
    fill.style.transform = vertical ? `scaleY(${pos})` : `scaleX(${pos})`;   // track is padded 8px, handle travels 15px..; close enough at 132px
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


/** Pan knob (DV-22): a small rotary — drag up/down or left/right, wheel, arrow keys, double-click = centre.
 *  value −1 (L) … +1 (R). Shows "C", "L35", "R80" like a desk. */
export function knob({ value = 0, label, probe, onInput, onCommit }) {
  const el = document.createElement("div");
  el.className = "knob"; el.setAttribute("role", "slider"); el.tabIndex = 0;
  el.setAttribute("aria-valuemin", "-100"); el.setAttribute("aria-valuemax", "100");
  if (label) el.setAttribute("aria-label", label);
  if (probe) el.dataset.probe = probe;
  el.innerHTML = '<svg viewBox="0 0 40 40" width="34" height="34"><path class="arc" d="M8 32 A16 16 0 1 1 32 32" fill="none"/><path class="val" fill="none"/><line class="pointer" x1="20" y1="20" x2="20" y2="7"/></svg><output></output>';
  const val = el.querySelector(".val"), ptr = el.querySelector(".pointer"), out = el.querySelector("output");
  let v = value, dragging = false, start = null, lastTap = 0;
  const text = (x) => (Math.abs(x) < 0.03 ? "C" : (x < 0 ? "L" : "R") + Math.round(Math.abs(x) * 100));
  const render = () => {
    const a = v * 135;                                    // −135° … +135°
    ptr.setAttribute("transform", `rotate(${a} 20 20)`);
    const r = 16, cx = 20, cy = 20, toXY = (deg) => { const t = (deg - 90) * Math.PI / 180; return [cx + r * Math.cos(t), cy + r * Math.sin(t)]; };
    const [x0, y0] = toXY(0), [x1, y1] = toXY(a);
    val.setAttribute("d", a === 0 ? "" : `M${x0} ${y0} A${r} ${r} 0 0 ${a > 0 ? 1 : 0} ${x1} ${y1}`);
    out.textContent = text(v);
    el.setAttribute("aria-valuenow", Math.round(v * 100)); el.setAttribute("aria-valuetext", text(v));
    el.dataset.value = v.toFixed(3);
  };
  const setV = (x, commit) => { v = Math.max(-1, Math.min(1, x)); render(); (commit ? onCommit : onInput)?.(v); };
  el.addEventListener("pointerdown", (ev) => {
    if (ev.button !== 0) return;
    const now = performance.now();
    if (now - lastTap < 350) { lastTap = 0; setV(0, true); return; }
    lastTap = now; dragging = true; start = { x: ev.clientX, y: ev.clientY, v }; el.setPointerCapture(ev.pointerId); el.classList.add("dragging"); ev.preventDefault();
  });
  el.addEventListener("pointermove", (ev) => { if (dragging) setV(start.v + ((ev.clientX - start.x) - (ev.clientY - start.y)) / 100, false); });
  const up = () => { if (!dragging) return; dragging = false; el.classList.remove("dragging"); setV(v, true); };
  el.addEventListener("pointerup", up); el.addEventListener("pointercancel", up);
  el.addEventListener("wheel", (ev) => { ev.preventDefault(); setV(v - Math.sign(ev.deltaY) * 0.05, true); }, { passive: false });
  el.addEventListener("keydown", (ev) => {
    const step = ev.shiftKey ? 0.25 : 0.05;
    if (ev.key === "ArrowLeft" || ev.key === "ArrowDown") setV(v - step, true);
    else if (ev.key === "ArrowRight" || ev.key === "ArrowUp") setV(v + step, true);
    else if (ev.key === "Home") setV(-1, true); else if (ev.key === "End") setV(1, true); else if (ev.key === "0" || ev.key === "c") setV(0, true);
    else return;
    ev.preventDefault();
  });
  el.update = (x) => { if (!dragging) { v = x; render(); } };
  render();
  return el;
}


/** Inline SVG icons (stroke = currentColor) — emoji render differently per font/OS, a desk needs the same glyph everywhere. */
const ICON_PATHS = {
  // ear (lucide "ear"): recognisable at 18 px
  ear: "M6 8.5a6.5 6.5 0 1 1 13 0c0 6-6 6-6 10a3.5 3.5 0 1 1-7 0 M15 8.5a2.5 2.5 0 0 0-5 0v1a2 2 0 1 1 0 4",
  headphones: "M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H3v-7zm18 0h-3a2 2 0 0 0-2 2v3a2 2 0 0 0 2 2h3v-7zM3 14a9 9 0 0 1 18 0",
  // link (two arrows head-to-tail): "this cell follows another mix"
  link: "M4 8h13M13 4l4 4-4 4M20 16H7M11 12l-4 4 4 4",
  more: "M12 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2zm0 6a1 1 0 1 1 0 2 1 1 0 0 1 0-2zm0 6a1 1 0 1 1 0 2 1 1 0 0 1 0-2z",
  plus: "M12 5v14M5 12h14",
};
export function svgIcon(name) {
  const s = document.createElementNS("http://www.w3.org/2000/svg", "svg"); s.setAttribute("viewBox", "0 0 24 24"); s.classList.add("i");
  const p = document.createElementNS("http://www.w3.org/2000/svg", "path"); p.setAttribute("d", ICON_PATHS[name] || ""); s.append(p); return s;
}
export function iconButton(name, opts) { const b = button("", opts); b.append(svgIcon(name)); return b; }
