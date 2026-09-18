// apps.js — UX-10/UX-11: every running application, its icon, whether it makes sound, and which channel it feeds.
import * as C from "./client.js";
import { meter, button, el, toast } from "./widgets.js";
import { icon } from "./mixer.js";

const err = (e) => toast(e.message, true);

export function render(root) {
  root.replaceChildren();
  const apps = C.apps(), chans = C.channels();
  const list = el("div", { class: "apps", probe: "appsList" });
  if (!apps.length) list.append(el("p", { class: "empty" }, "No application is playing audio right now. Apps appear here the moment they start a stream."));
  for (const a of apps) {
    const row = el("div", { class: "app" + (a.Running ? "" : " idle"), probe: `app/${a.NodeId}` });
    row.append(el("span", { class: "icon big", probe: `appIcon/${a.NodeId}` }, icon(a.Icon)));
    const body = el("div", { class: "body" });
    body.append(el("div", { class: "title" }, a.Name, el("span", { class: "dot" + (a.Running ? " on" : ""), probe: `appRunning/${a.NodeId}`, "data-value": String(!!a.Running), title: a.Running ? "playing" : "silent", role: "img", "aria-label": a.Running ? "playing" : "silent" })));
    if (a.MediaName && a.MediaName !== a.Name) body.append(el("div", { class: "sub" }, a.MediaName));
    body.append(meter(C.meterKey.app(a.NodeId), { horizontal: true, probe: `appMeter/${a.NodeId}` }));
    row.append(body);
    const target = el("div", { class: "target" });
    const cur = new Set(a.Channels?.length ? a.Channels : (a.Channel && a.Channel !== "/" ? [C.slugOf(a.Channel)] : []));
    if (!cur.size) target.append(el("span", { class: "unassigned", probe: `appUnassigned/${a.NodeId}` }, "not on a channel"));
    for (const ch of chans) {
      const on = cur.has(ch.Slug);
      target.append(button(ch.Name, { probe: `appTo/${a.NodeId}/${ch.Slug}`, cls: "chip" + (on ? " on" : ""), pressed: on, title: on ? `Remove from ${ch.Name}` : `Send to ${ch.Name}`, onClick: () => {
        const next = new Set(cur); on ? next.delete(ch.Slug) : next.add(ch.Slug);
        C.call(a.path, "Assign", [...next].map((slug) => `${C.ROOT}/channel/${slug}`), false).catch(err);   // Assign takes channel OBJECT PATHS
      } }));
    }
    row.append(target);
    list.append(row);
  }
  root.append(list);
}
