// mixer.js — UX-1: channels as rows against mixes as panels, one fader per cell. Same layout rules as MixerPage.qml.
import * as C from "./client.js";
import { fader, meter, button, el, toast } from "./widgets.js";

const ICONS = { microphone: "🎤", "audio-headphones": "🎧", "applications-games": "🎮", "audio-speakers": "🔊", "preferences-system": "⚙️",
                "media-playback-start": "▶", "audio-volume-high": "🔊", "camera-web": "📷", "internet-chat": "💬", "multimedia-player": "🎵", "": "" };
export const icon = (name) => ICONS[name] ?? (name.startsWith("data:") || name.includes("/") ? el("img", { src: name, alt: "", class: "icon-img" }) : "●");

function prompt2(label, current) { const v = window.prompt(label, current); return v === null || v.trim() === current ? null : v.trim(); }

export function render(root) {
  root.replaceChildren();
  const chans = C.channels(), mixes = C.mixes();
  const grid = el("div", { class: "mixer", probe: "mixStrip", style: { "--mixes": mixes.length } });

  // ---- column headers
  grid.append(el("div", { class: "corner" },
    button("+", { probe: "addChannel", title: "Add channel", cls: "add", onClick: async () => { const n = prompt2("Channel name", ""); if (n) await C.call(C.ROOT, "AddChannel", n).catch(err); } })));
  for (const m of mixes) grid.append(mixHeader(m));

  // ---- one row per channel
  for (const ch of chans) {
    grid.append(channelHeader(ch));
    for (const m of mixes) grid.append(cellView(ch, m));
  }
  grid.append(el("div", { class: "corner bottom" },
    button("+ mix", { probe: "addMix", title: "Add mix", cls: "add", onClick: async () => { const n = prompt2("Mix name", ""); if (n) await C.call(C.ROOT, "AddMix", n).catch(err); } })));
  root.append(grid);
  if (!chans.length) root.append(el("p", { class: "empty" }, "No channels yet — press + to add one, or run the first-run wizard from the KDE window."));
}
const err = (e) => toast(e.message, true);

function channelHeader(ch) {
  const slug = ch.Slug, h = el("div", { class: "channel-header", probe: `channelHeader/${slug}`, style: { "--accent": ch.Color || "transparent" } });
  h.dataset.slug = slug;
  h.append(el("div", { class: "stripe", probe: `channelColorStripe/${slug}` }));
  const name = el("button", { class: "name", probe: `channelName/${slug}`, title: "Rename", onclick: async () => { const n = prompt2("Channel name", ch.Name); if (n) await C.set(ch.path, "Name", n).catch(err); } }, el("span", { class: "icon" }, icon(ch.Icon)), " ", ch.Name);
  h.append(el("div", { class: "head-row" }, name, ch.Group ? el("span", { class: "badge", probe: "channelGroupBadge" }, ch.Group) : null));
  const src = el("div", { class: "source", probe: `channelSource/${slug}` }, ch.Inputs?.length ? ch.Inputs.join(", ") : "Apps", ch.InputPresent === false ? el("span", { class: "gone", title: "input device is not connected" }, " ⚠") : null);
  h.append(src);
  const controls = el("div", { class: "controls" });
  controls.append(button("M", { probe: `channelMute/${slug}`, cls: "mute", pressed: ch.Muted, title: "Mute channel", onClick: () => C.set(ch.path, "Muted", !ch.Muted).catch(err) }));
  controls.append(fader({ value: ch.Trim, max: 2, vertical: false, label: `${ch.Name} trim`, probe: `channelTrim/${slug}`,
    onInput: (v) => C.set(ch.path, "Trim", v).catch(() => {}), onCommit: (v) => C.set(ch.path, "Trim", v).catch(err) }));
  controls.append(button("👂", { probe: `channelListen/${slug}`, cls: "listen", title: "Hold to hear only this channel", onClick: () => {} }));
  hold(controls.lastChild, ch.path);
  controls.append(button("⋮", { probe: `channelMenuButton/${slug}`, cls: "menu", title: "More", onClick: (ev) => menu(ev.currentTarget, [
    ["Rename…", async () => { const n = prompt2("Channel name", ch.Name); if (n) await C.set(ch.path, "Name", n).catch(err); }],
    ["Colour…", async () => { const c = prompt2("Colour (#rrggbb or empty)", ch.Color); if (c !== null) await C.set(ch.path, "Color", c).catch(err); }],
    ["Group…", async () => { const g = prompt2("Group", ch.Group); if (g !== null) await C.set(ch.path, "Group", g).catch(err); }],
    ["Move up", () => C.call(C.ROOT, "MoveChannel", ch.path, Math.max(0, C.channels().findIndex((c) => c.path === ch.path) - 1)).catch(err)],
    ["Move down", () => C.call(C.ROOT, "MoveChannel", ch.path, C.channels().findIndex((c) => c.path === ch.path) + 1).catch(err)],
    ["Remove channel", () => confirm(`Remove channel “${ch.Name}”?`) && C.call(C.ROOT, "RemoveChannel", ch.path).catch(err), "danger"],
  ]) }));
  h.append(controls);
  h.append(el("div", { class: "pan-row" }, el("label", {}, "L"), panSlider(ch), el("label", {}, "R")));
  h.append(meter(C.meterKey.channel(slug), { horizontal: true, probe: `channelMeter/${slug}` }));
  return h;
}

function panSlider(ch) {
  const s = el("input", { type: "range", min: -1, max: 1, step: 0.05, value: ch.Pan ?? 0, probe: `channelPan/${ch.Slug}`, "aria-label": `${ch.Name} pan`, class: "pan" });
  s.addEventListener("input", () => C.set(ch.path, "Pan", Number(s.value)).catch(() => {}));
  s.addEventListener("change", () => C.set(ch.path, "Pan", Number(s.value)).catch(err));
  s.addEventListener("dblclick", () => { s.value = 0; C.set(ch.path, "Pan", 0).catch(err); });
  return s;
}

function mixHeader(m) {
  const slug = m.Slug, listening = C.state.root.ListeningDevice && m.Outputs?.includes(C.state.root.ListeningDevice);
  const h = el("div", { class: "mix-header" + (m.Muted ? " muted" : "") + (listening ? " hearing" : ""), probe: `mixHeader/${slug}`, style: { "--accent": m.Color || "transparent" } });
  h.dataset.slug = slug;
  h.append(el("div", { class: "stripe", probe: `mixColorStripe/${slug}` }));
  h.append(el("button", { class: "name", probe: `mixName/${slug}`, title: "Rename", onclick: async () => { const n = prompt2("Mix name", m.Name); if (n) await C.set(m.path, "Name", n).catch(err); } }, el("span", { class: "icon" }, icon(m.Icon)), " ", m.Name));
  const outs = m.OutputDescriptions?.length ? m.OutputDescriptions.join(" + ") : "no output";
  h.append(el("div", { class: "source", probe: `mixOutput/${slug}` }, outs, m.OutputPresent === false ? el("span", { class: "gone", title: "output device is not connected" }, " ⚠") : null));
  const controls = el("div", { class: "controls" });
  controls.append(button("M", { probe: `mixMute/${slug}`, cls: "mute", pressed: m.Muted, title: "Mute mix", onClick: () => C.set(m.path, "Muted", !m.Muted).catch(err) }));
  controls.append(fader({ value: m.Volume, max: 1, vertical: false, label: `${m.Name} master`, probe: `mixFader/${slug}`,
    onInput: (v) => C.set(m.path, "Volume", v).catch(() => {}), onCommit: (v) => C.set(m.path, "Volume", v).catch(err) }));
  const hear = button("🎧", { probe: `mixListen/${slug}`, cls: "listen" + (listening ? " on" : ""), pressed: !!listening, title: listening ? "You are hearing this mix" : "Hear this mix on your headphones",
    onClick: () => { const dev = m.Outputs?.[0]; if (dev) C.set(C.ROOT, "ListeningDevice", dev).catch(err); else toast("This mix has no output device yet", true); } });
  controls.append(hear);
  controls.append(button("⋮", { probe: `mixMenuButton/${slug}`, cls: "menu", title: "More", onClick: (ev) => menu(ev.currentTarget, [
    ["Rename…", async () => { const n = prompt2("Mix name", m.Name); if (n) await C.set(m.path, "Name", n).catch(err); }],
    ["Colour…", async () => { const c = prompt2("Colour (#rrggbb or empty)", m.Color); if (c !== null) await C.set(m.path, "Color", c).catch(err); }],
    ["Output device…", () => outputPicker(m)],
    ["Duplicate…", async () => { const n = prompt2("Name for the copy", `${m.Name} copy`); if (n) await C.call(C.ROOT, "DuplicateMix", m.path, n).catch(err); }, "", `mixDuplicate/${slug}`],
    ["Move left", () => C.call(C.ROOT, "MoveMix", m.path, Math.max(0, C.mixes().findIndex((x) => x.path === m.path) - 1)).catch(err)],
    ["Move right", () => C.call(C.ROOT, "MoveMix", m.path, C.mixes().findIndex((x) => x.path === m.path) + 1).catch(err)],
    ["Remove mix", () => confirm(`Remove mix “${m.Name}”?`) && C.call(C.ROOT, "RemoveMix", m.path).catch(err), "danger"],
  ]) }));
  h.append(controls);
  h.append(meter(C.meterKey.mix(slug), { horizontal: true, probe: `mixMeter/${slug}` }));
  return h;
}

function outputPicker(m) {
  const devs = Object.entries(C.state.root.OutputDevices || {});
  const box = el("div", { class: "popover", probe: `outMenu/${m.Slug}` });
  box.append(el("h3", {}, `${m.Name} → outputs`));
  for (const [node, desc] of devs) {
    const on = m.Outputs?.includes(node);
    box.append(el("label", { class: "row" }, el("input", { type: "checkbox", checked: on, onchange: (ev) => (ev.target.checked ? C.call(m.path, "AddOutput", node) : C.call(m.path, "RemoveOutput", node)).catch(err) }), " ", desc || node));
  }
  if (!devs.length) box.append(el("p", {}, "No output devices"));
  openPopover(box);
}

function cellView(ch, m) {
  const c = C.cell(ch.path, m.path);
  const key = `${ch.Slug}/${m.Slug}`;
  const v = el("div", { class: "cell" + (c?.Muted ? " muted" : "") + (c?.Follows ? " linked" : ""), probe: `cell/${key}` });
  if (!c) { v.append(el("span", { class: "pending" }, "…")); return v; }
  v.append(fader({ value: c.Volume, max: 1, label: `${ch.Name} in ${m.Name}`, probe: `cellFader/${key}`,
    onInput: (x) => C.set(c.path, "Volume", x).catch(() => {}), onCommit: (x) => C.set(c.path, "Volume", x).catch(err) }));
  v.append(meter(C.meterKey.cell(ch.Slug, m.Slug), { probe: `cellMeter/${key}` }));
  const foot = el("div", { class: "cell-foot" });
  foot.append(button("M", { probe: `cellMute/${key}`, cls: "mute", pressed: c.Muted, title: "Mute this channel in this mix", onClick: () => C.set(c.path, "Muted", !c.Muted).catch(err) }));
  const others = C.mixes().filter((x) => x.path !== m.path);
  const followsSlug = c.Follows && c.Follows !== "/" ? C.slugOf(c.Follows) : "";
  foot.append(button(followsSlug ? `⇄ ${followsSlug}` : "⇄", { probe: `cellLink/${key}`, cls: "link" + (followsSlug ? " on" : ""), pressed: !!followsSlug, title: followsSlug ? `Follows ${followsSlug} — click to unlink` : "Link to another mix", onClick: (ev) => {
    if (followsSlug) return C.set(c.path, "Follows", "/").catch(err);
    menu(ev.currentTarget, others.map((o) => [`Follow ${o.Name}`, () => C.set(c.path, "Follows", o.path).catch(err)]));
  } }));
  v.append(foot);
  return v;
}

// press-and-hold audition (UX-12): pointerdown → Audition(path), pointerup → Audition("/")
function hold(btn, path) {
  const start = (ev) => { ev.preventDefault(); btn.classList.add("on"); C.call(C.ROOT, "Audition", path).catch(err); };
  const stop = () => { if (!btn.classList.contains("on")) return; btn.classList.remove("on"); C.call(C.ROOT, "Audition", "/").catch(() => {}); };
  btn.addEventListener("pointerdown", start); btn.addEventListener("pointerup", stop); btn.addEventListener("pointercancel", stop); btn.addEventListener("pointerleave", stop);
  btn.addEventListener("click", (ev) => ev.preventDefault(), true);
}

// ---- tiny popover/menu machinery
let openEl = null;
export function closePopover() { openEl?.remove(); openEl = null; }
export function openPopover(box) { closePopover(); openEl = box; document.body.append(box); }
document.addEventListener("pointerdown", (ev) => { if (openEl && !openEl.contains(ev.target)) closePopover(); });
export function menu(anchor, items) {
  const box = el("div", { class: "popover menu", role: "menu" });
  for (const [label, action, cls, probe] of items) box.append(el("button", { role: "menuitem", class: cls || "", probe, onclick: () => { closePopover(); action(); } }, label));
  const r = anchor.getBoundingClientRect();
  Object.assign(box.style, { top: `${Math.min(r.bottom + 4, innerHeight - 40 * items.length - 16)}px`, left: `${Math.min(r.left, innerWidth - 220)}px` });
  openPopover(box);
}
