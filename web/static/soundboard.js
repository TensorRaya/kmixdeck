// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
// soundboard.js — CT-8: jingles and stingers on a button. Same data as `kmixdeck sample list` and
// SoundboardPanel.qml: Mixer.Samples, rows of {channel, name, path, length, gain, sounding}.
//
// Why a view and not a drawer like fx/duck: a board is the thing you look at while you use it, and the buttons
// have to be big enough to hit without aiming — on a phone at that. A drawer over the mixer would fight for the
// same space the faders need.
//
// The board is opt-in: with no soundboard channel there is nothing to show, and the tab hides itself (same rule
// the scene picker in index.html follows).
import * as C from "./client.js";
import { el, button, toast } from "./widgets.js";

/** Rows straight from the Mixer property — never cached, so a patch is visible immediately. */
export const samples = () => C.state.root.Samples || [];
export const boards = () => C.channels().filter((c) => c.Kind === "soundboard");
export const hasBoard = () => boards().length > 0;
const samplesOf = (slug) => samples().filter((s) => s.channel === slug);

const fmtLength = (s) => (s > 0 ? `${s.toFixed(1)} s` : "");

async function play(s) {
  // PlaySampleOn, not PlaySample: the name is unique per board, not globally, and clicking a button on THIS
  // board must never start the same-named sample on another one.
  await C.call(C.ROOT, "PlaySampleOn", s.channel, s.name).catch((e) => toast(e.message, true));
}
async function stop(s) {
  await C.call(C.ROOT, "StopSample", s.name).catch((e) => toast(e.message, true));
}
async function remove(s) {
  if (!confirm(`Remove sample “${s.name}”?`)) return;
  await C.call(C.ROOT, "RemoveSample", s.channel, s.name).catch((e) => toast(e.message, true));
}
async function addTo(slug) {
  // The daemon opens the file, so it needs a path it can reach — a browser upload would land in the browser's
  // sandbox, not on the machine running the mixer. Hence a path, and the daemon refuses what it cannot decode.
  const path = prompt("Path to an audio file on the mixer machine (wav, flac, ogg, mp3):");
  if (!path) return;
  await C.call(C.ROOT, "AddSample", slug, path, "").catch((e) => toast(e.message, true));
}
async function setGain(s, linear) {
  await C.call(C.ROOT, "SetSampleGain", s.channel, s.name, linear).catch((e) => toast(e.message, true));
}

function pad(s) {
  const sounding = !!s.sounding;
  const b = button(s.name, {
    probe: `samplePad:${s.channel}:${s.name}`,
    cls: `sample-pad${sounding ? " sounding" : ""}`,
    title: `${s.path}${s.length > 0 ? ` — ${fmtLength(s.length)}` : ""}\nClick to play, click again to stop`,
    // One control for both directions: hunting for a separate stop button while a 30 s bed is running is
    // exactly the moment you do not want to aim.
    onClick: () => (sounding ? stop(s) : play(s)),
  });
  b.append(el("span", { class: "sample-len" }, fmtLength(s.length)));
  return b;
}

function row(s) {
  // Gain is linear 0…4 like the CLI, shown as a small slider under the pad so a quiet jingle can be lifted
  // without touching the channel fader (which would move every sample on the board).
  const g = el("input", {
    type: "range", min: "0", max: "4", step: "0.05", value: String(s.gain ?? 1),
    class: "sample-gain", "data-probe": `sampleGain:${s.channel}:${s.name}`,
    title: "Per-sample gain (does not move the channel fader)",
  });
  g.addEventListener("change", () => setGain(s, Number(g.value)));
  return el("div", { class: "sample-cell" }, pad(s), g,
            button("×", { probe: `sampleRemove:${s.channel}:${s.name}`, cls: "sample-del",
                          title: "Remove this sample", onClick: () => remove(s) }));
}

export function render(host) {
  host.innerHTML = "";
  const bs = boards();
  if (!bs.length) {
    // Opt-in: no board, no panel. Say how to get one instead of showing an empty grid.
    host.append(el("div", { class: "empty" },
      el("p", {}, "No soundboard yet."),
      el("p", { class: "hint" }, "A soundboard is a channel that plays files. Create one with ",
        el("code", {}, "kmixdeck channel add --soundboard Board"),
        " — it then has a fader in every mix like any other channel.")));
    return;
  }
  for (const b of bs) {
    const rows = samplesOf(b.Slug);
    host.append(el("section", { class: "board", "data-probe": `board:${b.Slug}` },
      el("header", {},
        el("h2", {}, b.Name),
        button("+ Sample", { probe: `sampleAdd:${b.Slug}`, cls: "sample-add",
                             title: "Register an audio file on this board", onClick: () => addTo(b.Slug) }),
        // Panic button: during a show, "stop whatever is playing" must not require finding which pad is lit.
        button("Stop all", { probe: `sampleStopAll:${b.Slug}`, cls: "sample-stopall",
                             title: "Stop every sample that is sounding",
                             onClick: () => C.call(C.ROOT, "StopSample", "").catch((e) => toast(e.message, true)) })),
      rows.length
        ? el("div", { class: "pads" }, ...rows.map(row))
        : el("p", { class: "hint" }, "No samples on this board yet — “+ Sample” takes a file path.")));
  }
}
