// patchbay.js — UX-15/DV-24: the whole signal path as one picture. Four columns of cards (sources · channels · mixes ·
// outputs), wires as S-curves between jacks. The model is derived here from the object tree with exactly the rules of
// MixerClient::patchbay() (AR-9) — same card ids, same wire kinds, same meter keys.
import * as C from "./client.js";
import { meter, el, toast, button } from "./widgets.js";
import { icon, menu } from "./mixer.js";

const err = (e) => toast(e.message, true);
const refNode = (ref) => ref.split(":")[0];
const refPositions = (ref) => (ref.split(":")[1] || "").split(">")[0].split(",").filter(Boolean);
const refSide = (ref) => (ref.includes(">") ? ref.split(">").pop() : "");

export function model() {
  const cards = [], wires = [];
  const root = C.state.root, chans = C.channels(), mixes = C.mixes();
  const row = (pos, label, meterKey, jackIn, jackOut, usedBy = "") => ({ pos, label, meterKey, jackIn, jackOut, usedBy });
  const card = (id, kind, title, icn, on, present, rows, node = "") => cards.push({ id, kind, title, icon: icn, on, present, rows, node });
  const used = {};
  for (const c of chans) for (const ref of c.Inputs || []) for (const p of refPositions(ref)) used[`${refNode(ref)}|${p}`] = c.Name;
  for (const m of mixes) for (const ref of m.Outputs || []) for (const p of refPositions(ref)) used[`${refNode(ref)}|${p}`] = m.Name;
  const ports = root.DevicePorts || {};
  const devRows = (node, jackIn, jackOut) => {
    const ps = ports[node] || [];
    if (!ps.length) return [row("FL", "L", `dev/${node}`, jackIn, jackOut, used[`${node}|FL`] || ""), row("FR", "R", `dev/${node}`, jackIn, jackOut, used[`${node}|FR`] || "")];
    // "position|name|alias" (Mixer::devicePorts); label = last ":"-section of the alias, else the position — as MixerClient::devicePorts()
    return ps.map((p) => { const f = p.split("|"); const pos = f[0]; let label = (f[2] || "").split(":").pop(); if (!label || label === f[1]) label = pos;
      return row(pos, label, `dev/${node}`, jackIn, jackOut, used[`${node}|${pos}`] || ""); });
  };
  // column 1: apps + input devices
  for (const a of C.apps()) {
    const id = `app/${a.path}`, mk = `app/${a.NodeId}`;
    card(id, "app", a.Name, a.Icon || "applications-multimedia", true, !!a.Running, [row("L", "1 (L)", mk, false, true), row("R", "2 (R)", mk, false, true)]);
    const cs = a.Channels?.length ? a.Channels : (a.Channel && a.Channel !== "/" ? [C.slugOf(a.Channel)] : []);
    for (const c of cs) for (const side of ["L", "R"]) wires.push({ from: { card: id, pos: side }, to: { card: `ch/${c}`, pos: side }, ref: "", kind: "app", muted: false, meterKey: mk, app: a.path, channel: c });
  }
  const hidden = new Set(root.HiddenDevices || []);
  const inUse = (node) => chans.some((c) => (c.Inputs || []).some((r) => refNode(r) === node)) || mixes.some((m) => (m.Outputs || []).some((r) => refNode(r) === node));
  for (const [node, desc] of Object.entries(root.InputDevices || {})) {
    if (hidden.has(node) && !inUse(node)) continue;
    card(`dev/${node}`, "device", desc, "audio-input-microphone", true, true, devRows(node, false, true), node);
  }
  // column 2: channels
  for (const c of chans) {
    card(`ch/${c.Slug}`, "channel", c.Name, c.Icon, !c.Muted, true, [row("L", "L", `channel/${c.Slug}`, true, true), row("R", "R", `channel/${c.Slug}`, true, true)]);
    for (const ref of c.Inputs || []) {
      const node = refNode(ref), pos = refPositions(ref), side = refSide(ref), present = node in (root.InputDevices || {});
      const wire = (fromPos, toSide) => wires.push({ from: { card: `dev/${node}`, pos: fromPos }, to: { card: `ch/${c.Slug}`, pos: toSide }, ref, kind: "input", muted: !present, meterKey: `in/${c.Slug}`, channel: c.Slug });
      if (!pos.length) { wire("FL", "L"); wire("FR", "R"); }
      else if (pos.length === 2) { wire(pos[0], "L"); wire(pos[1], "R"); }
      else if (side) wire(pos[0], side);
      else { wire(pos[0], "L"); wire(pos[0], "R"); }
    }
    for (const m of mixes) {
      const cell = C.cell(c.path, m.path);
      const muted = (cell?.Muted ?? false) || c.Muted;
      for (const side of ["L", "R"]) wires.push({ from: { card: `ch/${c.Slug}`, pos: side }, to: { card: `mix/${m.Slug}`, pos: side }, ref: "", kind: "cell", muted, gain: cell?.Volume ?? 1, meterKey: `cell/${c.Slug}/${m.Slug}`, channel: c.Slug, mix: m.Slug, cellPath: cell?.path });
    }
  }
  // column 3: mixes + captures; column 4: output devices
  const outDevs = new Set(Object.keys(root.OutputDevices || {}));
  for (const m of mixes) {
    card(`mix/${m.Slug}`, "mix", m.Name, m.Icon, !m.Muted, true, [row("L", "L", `mix/${m.Slug}`, true, true), row("R", "R", `mix/${m.Slug}`, true, true)]);
    for (const ref of m.Outputs || []) {
      const node = refNode(ref), pos = refPositions(ref), side = refSide(ref), present = node in (root.OutputDevices || {});
      outDevs.add(node);
      const wire = (fromSide, toPos) => wires.push({ from: { card: `mix/${m.Slug}`, pos: fromSide }, to: { card: `outdev/${node}`, pos: toPos }, ref, kind: "output", muted: m.Muted || !present, meterKey: `out/${m.Slug}`, mix: m.Slug });
      if (!pos.length) { wire("L", "FL"); wire("R", "FR"); }
      else if (pos.length === 2) { wire("L", pos[0]); wire("R", pos[1]); }
      else if (side) wire(side, pos[0]);
      else { wire("L", pos[0]); wire("R", pos[0]); }
    }
    const cid = `out/${m.Slug}/capture`;
    card(cid, "capture", `Capture: ${m.Name}`, "camera-video", true, true, [row("L", "L", `mix/${m.Slug}`, true, false), row("R", "R", `mix/${m.Slug}`, true, false)]);
    for (const side of ["L", "R"]) wires.push({ from: { card: `mix/${m.Slug}`, pos: side }, to: { card: cid, pos: side }, ref: "", kind: "capture", muted: m.Muted, meterKey: `mix/${m.Slug}`, mix: m.Slug });
  }
  const desc = (n) => (root.OutputDevices || {})[n] || n;
  for (const node of [...outDevs].sort((a, b) => desc(a).localeCompare(desc(b)))) {
    if (hidden.has(node) && !inUse(node)) continue;
    card(`outdev/${node}`, "output", desc(node), node === root.ListeningDevice ? "audio-headphones" : "audio-speakers", true, node in (root.OutputDevices || {}), devRows(node, true, false), node);
  }
  return { cards, wires };
}

const COLS = { app: 0, device: 0, channel: 1, mix: 2, capture: 3, output: 3 };

export function render(root) {
  root.replaceChildren();
  const { cards, wires } = model();
  const page = el("div", { class: "patchbay", probe: "patchbay" });
  const cols = [0, 1, 2, 3].map((i) => el("div", { class: "pb-col", "data-col": i }));
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg"); svg.classList.add("wires");
  const jacks = {};
  for (const c of cards) {
    const cardEl = el("div", { class: `card ${c.kind}` + (c.on ? "" : " off") + (c.present ? "" : " absent"), probe: `pbCard/${c.id}`, "data-id": c.id });
    cardEl.append(el("div", { class: "card-title" }, el("span", { class: "icon" }, icon(c.icon)), " ", c.title, c.present ? null : el("span", { class: "gone", title: "not connected" }, " ⚠")));
    for (const r of c.rows) {
      const rowEl = el("div", { class: "jack-row" });
      const jin = el("span", { class: "jack in" + (r.jackIn ? "" : " none"), "data-card": c.id, "data-pos": r.pos, "data-dir": "in" });
      const jout = el("span", { class: "jack out" + (r.jackOut ? "" : " none"), "data-card": c.id, "data-pos": r.pos, "data-dir": "out" });
      rowEl.append(jin, el("span", { class: "jack-label" }, r.label, r.usedBy ? el("small", {}, ` → ${r.usedBy}`) : null), jout);
      cardEl.append(rowEl);
      jacks[`${c.id}|${r.pos}|in`] = jin; jacks[`${c.id}|${r.pos}|out`] = jout;
    }
    cols[COLS[c.kind]].append(cardEl);
  }
  page.append(...cols, svg);
  root.append(page);
  // The cards are in the document now, so getBoundingClientRect() forces layout and the wires can be drawn in the SAME
  // render — no wire-less frame. (Until 2026-09-18 this waited for requestAnimationFrame; a burst of patches — AddInput
  // → Inputs, InputPresent, node names — re-rendered several times in a row and every render started with an empty
  // svg: visible flicker, and a test that clicked a wire found none.) A resize still needs a redraw: the jacks move.
  drawWires(page, svg, wires, jacks);
  wireDrag(page, jacks);
  page._wires = wires;
  page._redraw = () => drawWires(page, svg, wires, jacks);
  if (!render._ro) { render._ro = new ResizeObserver(() => document.querySelector(".patchbay")?._redraw?.()); }
  render._ro.disconnect(); render._ro.observe(page);
}

function centre(elm, page) { const r = elm.getBoundingClientRect(), p = page.getBoundingClientRect(); return [r.left + r.width / 2 - p.left + page.scrollLeft, r.top + r.height / 2 - p.top + page.scrollTop]; }

function drawWires(page, svg, wires, jacks) {
  svg.replaceChildren(); svg.setAttribute("width", page.scrollWidth); svg.setAttribute("height", page.scrollHeight);
  for (const w of wires) {
    const a = jacks[`${w.from.card}|${w.from.pos}|out`], b = jacks[`${w.to.card}|${w.to.pos}|in`];
    if (!a || !b) continue;
    const [x1, y1] = centre(a, page), [x2, y2] = centre(b, page), dx = Math.max(24, (x2 - x1) / 2);
    const p = document.createElementNS("http://www.w3.org/2000/svg", "path");
    p.setAttribute("d", `M${x1},${y1} C${x1 + dx},${y1} ${x2 - dx},${y2} ${x2},${y2}`);
    p.setAttribute("class", `wire ${w.kind}` + (w.muted ? " muted" : ""));
    if (w.gain !== undefined) p.style.strokeWidth = `${1 + 3 * Math.cbrt(w.gain)}px`;
    p.dataset.probe = `wire/${w.from.card}/${w.from.pos}/${w.to.card}/${w.to.pos}`;
    p.addEventListener("click", (ev) => wireMenu(ev, w));
    svg.append(p);
  }
}

function wireMenu(ev, w) {
  const items = [];
  if (w.kind === "cell" && w.cellPath) items.push([w.muted ? "Unmute in this mix" : "Mute in this mix", () => C.set(w.cellPath, "Muted", !w.muted).catch(err)]);
  if (w.kind === "input") items.push(["Remove wire", () => C.call(`${C.ROOT}/channel/${w.channel}`, "RemoveInput", w.ref).catch(err), "danger"]);
  if (w.kind === "output") items.push(["Remove wire", () => C.call(`${C.ROOT}/mix/${w.mix}`, "RemoveOutput", w.ref).catch(err), "danger"]);
  if (w.kind === "app") items.push(["Take app off this channel", () => { const a = C.state.objects[w.app]; const rest = (a?.Channels || []).filter((c) => c !== w.channel).map((slug) => `${C.ROOT}/channel/${slug}`); C.call(w.app, "Assign", rest, false).catch(err); }, "danger"]);
  if (items.length) menu({ getBoundingClientRect: () => ({ bottom: ev.clientY, left: ev.clientX }) }, items);
}

// drag from an OUT jack onto an IN jack (DV-24): device→channel = AddInput, mix→device = AddOutput, app→channel = Assign
function wireDrag(page, jacks) {
  let from = null, ghost = null;
  page.addEventListener("pointerdown", (ev) => {
    const j = ev.target.closest(".jack.out:not(.none)"); if (!j) return;
    from = j; ghost = document.createElementNS("http://www.w3.org/2000/svg", "path"); ghost.setAttribute("class", "wire ghost"); page.querySelector("svg.wires").append(ghost);
    page.setPointerCapture(ev.pointerId); ev.preventDefault();
  });
  page.addEventListener("pointermove", (ev) => {
    if (!from) return;
    const [x1, y1] = centre(from, page), p = page.getBoundingClientRect(), x2 = ev.clientX - p.left + page.scrollLeft, y2 = ev.clientY - p.top + page.scrollTop, dx = Math.max(24, (x2 - x1) / 2);
    ghost.setAttribute("d", `M${x1},${y1} C${x1 + dx},${y1} ${x2 - dx},${y2} ${x2},${y2}`);
  });
  const end = (ev) => {
    if (!from) return;
    const target = document.elementFromPoint(ev.clientX, ev.clientY)?.closest(".jack.in:not(.none)");
    ghost.remove(); const f = from; from = null; ghost = null;
    if (!target) return;
    connect(f.dataset.card, f.dataset.pos, target.dataset.card, target.dataset.pos);
  };
  page.addEventListener("pointerup", end); page.addEventListener("pointercancel", () => { ghost?.remove(); from = null; ghost = null; });
}

export function connect(fromCard, fromPos, toCard, toPos) {
  const side = toPos === "L" || toPos === "R" ? toPos : "";
  if (fromCard.startsWith("dev/") && toCard.startsWith("ch/")) {
    const node = fromCard.slice(4), ch = toCard.slice(3);
    // stereo pair when dragging FL→L: the window does the same (DV-24); a single port onto one side = side-bound mono
    const ref = (fromPos === "FL" && side === "L") ? node : `${node}:${fromPos}` + (side ? `>${side}` : "");
    return C.call(`${C.ROOT}/channel/${ch}`, "AddInput", ref).catch(err);
  }
  if (fromCard.startsWith("mix/") && toCard.startsWith("outdev/")) {
    const mix = fromCard.slice(4), node = toCard.slice(7);
    const ref = (toPos === "FL" && fromPos === "L") ? node : `${node}:${toPos}` + (fromPos === "L" || fromPos === "R" ? `>${fromPos}` : "");
    return C.call(`${C.ROOT}/mix/${mix}`, "AddOutput", ref).catch(err);
  }
  if (fromCard.startsWith("app/") && toCard.startsWith("ch/")) {
    const path = fromCard.slice(4), ch = toCard.slice(3), a = C.state.objects[path];
    return C.call(path, "Assign", [...new Set([...(a?.Channels || []), ch])].map((slug) => `${C.ROOT}/channel/${slug}`), false).catch(err);   // object paths, not slugs
  }
  toast("These two cannot be wired", true);
}
