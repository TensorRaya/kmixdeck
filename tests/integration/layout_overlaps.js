// SPDX-License-Identifier: GPL-3.0-or-later
// Layout check for the web frontend, the same rules as layoutOverlaps() in Main.qml: what a user reads or presses
// (a text run, a button, a select, a slider) — stop there, a button's inner icon is not a separate finding. Two
// kinds of finding: two such items overlap, or one sticks out of the card it belongs to (channel header, mix
// header, cell). Clipped parts count as not visible (overflow hidden really hides them).
(() => {
  const min = 2;
  const card = (e) => e.closest(".channel-header, .mix-header, .cell");
  const visibleRect = (e) => {
    const cs = getComputedStyle(e);
    if (cs.visibility === "hidden" || cs.display === "none" || +cs.opacity === 0) return null;
    let r = e.getBoundingClientRect();
    if (r.width < 2 || r.height < 2) return null;
    let x1 = r.left, y1 = r.top, x2 = r.right, y2 = r.bottom;
    for (let p = e.parentElement; p; p = p.parentElement) {
      const ps = getComputedStyle(p);
      if (+ps.opacity === 0) return null;
      if (/(hidden|auto|scroll|clip)/.test(ps.overflowX + ps.overflowY)) {
        const q = p.getBoundingClientRect();
        x1 = Math.max(x1, q.left); y1 = Math.max(y1, q.top); x2 = Math.min(x2, q.right); y2 = Math.min(y2, q.bottom);
        if (x2 - x1 < 1 || y2 - y1 < 1) return null;
      }
    }
    return { left: x1, top: y1, right: x2, bottom: y2, full: r };
  };
  const leafs = [];
  const control = "button, select, input, [role=slider], .fader, .knob";
  const walk = (e) => {
    if (e.matches(control)) { const r = visibleRect(e); if (r) leafs.push({ e, r }); return; }
    const hasText = [...e.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
    if (hasText) { const r = visibleRect(e); if (r) leafs.push({ e, r }); }
    for (const c of e.children) walk(c);
  };
  walk(document.getElementById("view") || document.body);
  const nm = (e) => (e.dataset.probe || e.tagName.toLowerCase() + (typeof e.className === "string" && e.className ? "." + e.className.split(" ")[0] : ""))
    + (e.textContent.trim() ? ' "' + e.textContent.trim().slice(0, 24) + '"' : "");
  const out = [];
  for (let i = 0; i < leafs.length; i++) for (let j = i + 1; j < leafs.length; j++) {
    const a = leafs[i], b = leafs[j];
    if (a.e.contains(b.e) || b.e.contains(a.e)) continue;
    const ox = Math.min(a.r.right, b.r.right) - Math.max(a.r.left, b.r.left), oy = Math.min(a.r.bottom, b.r.bottom) - Math.max(a.r.top, b.r.top);
    if (ox >= min && oy >= min) out.push(`${nm(a.e)} @${Math.round(a.r.left)},${Math.round(a.r.top)} ⨯ ${nm(b.e)} @${Math.round(b.r.left)},${Math.round(b.r.top)} = ${Math.round(ox)}x${Math.round(oy)}`);
  }
  for (const a of leafs) {
    const c = card(a.e); if (!c) continue;
    const q = c.getBoundingClientRect(), f = a.r.full;
    const raus = Math.max(q.left - f.left, f.right - q.right, q.top - f.top, f.bottom - q.bottom);
    if (raus >= min) out.push(`OUTSIDE ${nm(a.e)} @${Math.round(f.left)},${Math.round(f.top)} leaves ${c.dataset.probe || c.className} by ${Math.round(raus)} px`);
  }
  // Text squeezed below its own width without an ellipsis: it is cut mid-glyph or runs over its neighbours.
  for (const a of leafs) {
    const e = a.e, cs = getComputedStyle(e);
    if (e.scrollWidth > e.clientWidth + 2 && e.clientWidth > 0 && cs.textOverflow !== "ellipsis" && e.tagName !== "SELECT")
      out.push(`SQUEEZED ${nm(e)} ${e.scrollWidth}>${e.clientWidth}`);
    if (e.tagName === "SELECT" && e.getBoundingClientRect().width < 60) out.push(`SQUEEZED ${nm(e)} ${Math.round(e.getBoundingClientRect().width)} px`);
  }
  return out.length + (out.length ? "\n" + out.join("\n") : "");
})()
