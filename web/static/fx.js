// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
// fx.js — FX-1…FX-7 / ADR 0008: the effect chain of one channel or mix, as a drawer. Same model as FxPanel.qml:
// chain = {enabled, chain:[{type, enabled, params:{key: value}}]}; the catalog (FxTypes) and presets (FxPresets) come
// from the daemon — nothing about gates or compressors is known here.
import * as C from "./client.js";
import { el, button, iconButton, toast } from "./widgets.js";

let open = null;   // { path, kind, title }

export function fxTypes() { try { return JSON.parse(C.state.root.FxTypes || "[]"); } catch { return []; } }
export function fxPresets() { try { return JSON.parse(C.state.root.FxPresets || "{}"); } catch { return {}; } }
export function chainOf(obj) { try { const c = JSON.parse(obj.FxChain || ""); return c && c.chain ? c : { enabled: false, chain: [] }; } catch { return { enabled: false, chain: [] }; } }
export function fxActive(obj) { const c = chainOf(obj); return !!(c.enabled && c.chain.length); }

export function openFx(obj, title) { open = { path: obj.path, title }; render(); }
export function closeFx() { open = null; render(); }

async function save(chain) {
  if (!C.state.objects[open.path]) return;
  await C.call(open.path, "SetFx", JSON.stringify(chain)).catch((e) => toast(e.message, true));
}

export function render() {
  const host = document.getElementById("fx-drawer");
  if (!host) return;
  host.innerHTML = "";
  if (!open) { host.hidden = true; document.body.classList.remove("fx-open"); return; }
  const obj = C.state.objects[open.path] && { path: open.path, ...C.state.objects[open.path] };   // state entries carry no .path (byIface adds it)
  if (!obj) { open = null; host.hidden = true; document.body.classList.remove("fx-open"); return; }
  host.hidden = false; document.body.classList.add("fx-open");
  const chain = chainOf(obj), types = fxTypes(), presets = fxPresets();
  host.dataset.probe = "fxPanel"; host.dataset.value = obj.path;

  const head = el("div", { class: "fx-head" },
    el("h2", {}, `Effects on ${open.title}`),
    el("label", { class: "switch" },
      Object.assign(el("input", { type: "checkbox", probe: "fxEnabled" }), { checked: chain.enabled, onchange: (ev) => save({ ...chain, enabled: ev.target.checked }) }),
      el("span", {}, "Enabled")),
    iconButton("close", { title: "Close", cls: "menu", probe: "fxClose", onClick: closeFx }));
  host.append(head);

  const list = el("div", { class: "fx-list", probe: "fxList" }); list.dataset.value = String(chain.chain.length);
  chain.chain.forEach((fx, i) => {
    const spec = types.find((t) => t.type === fx.type);
    const card = el("div", { class: "fx-card" + (fx.enabled === false ? " off" : ""), probe: `fxCard/${i}` }); card.dataset.value = fx.type;
    const row = el("div", { class: "fx-card-head" },
      el("label", { class: "switch" },
        Object.assign(el("input", { type: "checkbox", probe: `fxCardEnabled/${i}` }), { checked: fx.enabled !== false, onchange: (ev) => { const c = clone(chain); c.chain[i].enabled = ev.target.checked; save(c); } }),
        el("strong", {}, spec?.label || fx.type)),
      el("span", { class: "spacer" }),
      iconButton("up", { title: "Move up", cls: "menu", probe: `fxUp/${i}`, onClick: () => { if (i === 0) return; const c = clone(chain); [c.chain[i - 1], c.chain[i]] = [c.chain[i], c.chain[i - 1]]; save(c); } }),
      iconButton("trash", { title: "Remove effect", cls: "menu danger", probe: `fxRemove/${i}`, onClick: () => { const c = clone(chain); c.chain.splice(i, 1); save(c); } }));
    card.append(row);
    if (spec?.description) card.append(el("p", { class: "fx-desc" }, spec.description));
    for (const p of spec?.params || []) {
      const cur = fx.params && p.key in fx.params ? fx.params[p.key] : p.def;
      const out = el("output", {}, fmt(cur, p.unit));
      const slider = Object.assign(el("input", { type: "range", min: p.min, max: p.max, step: stepFor(p), probe: `fxParam/${i}/${p.key}`, "aria-label": `${spec.label} ${p.label}` }), { value: cur });
      slider.dataset.value = String(cur);
      // live: SetFxControl on input (glitch-free Props write, ADR 0008 finding 2); the chain JSON is saved on change
      slider.oninput = () => { out.textContent = fmt(+slider.value, p.unit); slider.dataset.value = slider.value; C.call(obj.path, "SetFxControl", p.key, +slider.value).catch(() => {}); };   // spec key, resolved by the daemon (Mixer::setFxControl)
      slider.onchange = () => { const c = clone(chain); c.chain[i].params = c.chain[i].params || {}; c.chain[i].params[p.key] = Math.round(+slider.value * 100) / 100; save(c); };
      card.append(el("div", { class: "fx-param" }, el("label", {}, p.label), slider, out));
    }
    list.append(card);
  });
  host.append(list);

  const add = el("div", { class: "fx-add" });
  const sel = el("select", { probe: "fxAddType", "aria-label": "Effect to add" });
  sel.append(el("option", { value: "" }, "Add effect…"));
  // FX-8: an effect whose LADSPA plugin is missing is offered as a DISABLED option with the
  // package name in the label, not as a normal choice. Before 2026-09-21 it looked pickable,
  // the daemon refused the whole chain, and the package name only went to the daemon log —
  // the one person who could fix it was the one person not told how. `disabled` is also the
  // honest markup here: a screen reader announces it, a grey label alone does not.
  for (const t of types) {
    const fehlt = t.available === false;
    const attrs = { value: t.type, title: fehlt ? `Install ${t.package} to use this effect` : (t.description || "") };
    if (fehlt) attrs.disabled = "";
    sel.append(el("option", attrs, fehlt ? `${t.label} — needs ${t.package}` : t.label));
  }
  sel.onchange = () => { if (!sel.value) return; const c = clone(chain); c.chain.push({ type: sel.value, enabled: true, params: {} }); c.enabled = true; save(c); sel.value = ""; };
  add.append(sel);
  host.append(add);

  const pre = el("div", { class: "fx-presets" }, el("span", { class: "muted" }, "Presets:"));
  for (const name of Object.keys(presets)) pre.append(button(name, { probe: `fxPreset/${name}`, title: "Replace the chain with this preset (editable afterwards)", onClick: () => save(presets[name]) }));
  if (chain.chain.length) pre.append(button("Clear", { cls: "danger", probe: "fxClear", title: "Remove every effect", onClick: () => save({ enabled: false, chain: [] }) }));
  host.append(pre);
}

const clone = (c) => JSON.parse(JSON.stringify(c));
const fmt = (v, unit) => (Number.isInteger(v) ? String(v) : (+v).toFixed(2).replace(/\.?0+$/, "")) + (unit ? " " + unit : "");
const stepFor = (p) => { const r = p.max - p.min; return r > 100 ? 1 : r > 10 ? 0.1 : 0.01; };
