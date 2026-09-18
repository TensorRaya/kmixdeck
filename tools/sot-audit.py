#!/usr/bin/env python3
"""VF-2 + AR-12 audit, wired into ctest.

1. every ✅ row in docs/spec/requirements.md names at least one existing test_* (or a rule row that names its
   enforcement);
2. ADR 0010 D3/D5: every ✅ row tagged `tier:core` must appear as an ID in tests/integration/test_frontends_sync.py's
   CORE table — that table drives the change through the CLI and reads it back from the KDE window AND the tray.
   A core feature without a row there is not done, whatever the daemon test says.
Exit 1 on violations."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
sot = (root / "docs/spec/requirements.md").read_text()
have = set()
for f in (root / "tests/integration").glob("*.py"):
    have |= set(re.findall(r"def (test_[a-z0-9_]+)", f.read_text()))
# unit tests count too (v0.2): a row may cite `tests/unit/<name>.cpp` — the file must exist and be a registered ctest
unit_files = {p.name for p in (root / "tests/unit").glob("*.cpp")}
unit_registered = set(re.findall(r"ecm_add_test\(unit/([a-z0-9_]+\.cpp)", (root / "tests/CMakeLists.txt").read_text()))
sync = (root / "tests/integration/test_frontends_sync.py").read_text()
core_covered = set(re.findall(r'^\s*\("([A-Z]{2}-\d+)\b', sync, re.M))
bad, missing, core_missing = [], [], []
for line in sot.splitlines():
    m = re.match(r"\| ([A-Z]{2}-\d+) \|", line)
    if not m or "✅" not in line: continue
    rid = m.group(1)
    refs = re.findall(r"test_[a-z0-9_]+", line)
    unit_refs = re.findall(r"tests/unit/([a-z0-9_]+\.cpp)", line)
    if not refs and not unit_refs and not re.search(r"enforced by|ctest|rule;", line): bad.append(rid); continue
    for r in refs:
        if r not in have and not any(h.startswith(r) for h in have): missing.append((rid, r))
    for u in unit_refs:
        if u not in unit_files or u not in unit_registered: missing.append((rid, "tests/unit/" + u))
    if "tier:core" in line and rid not in core_covered: core_missing.append(rid)
rows = [l for l in sot.splitlines() if re.match(r"\| [A-Z]{2}-\d+ \|", l)]
core_rows = [l for l in rows if "tier:core" in l]
# RQ-1 (review 2026-09-18): the count in the head of the SoT is not typed by hand — the audit fails when it disagrees,
# and `--write` puts the current numbers there. A number that can go stale must have exactly one author.
counts = f"{sum('✅' in r for r in rows)} ✅ · {sum('🔶' in r for r in rows)} 🔶 · {sum('📝' in r for r in rows)} 📝 ({len(rows)} rows)"
head_re = re.compile(r"^\*\*Status:\*\* .*?\(\d+ rows\)", re.M)
if "--write" in sys.argv:
    new = head_re.sub(f"**Status:** {counts}", sot, count=1)
    if new != sot: (root / "docs/spec/requirements.md").write_text(new); print("head updated:", counts)
elif not head_re.search(sot) or f"**Status:** {counts}" not in sot:
    bad.append("HEAD"); print(f"the head of requirements.md does not say '**Status:** {counts}' — run tools/sot-audit.py --write")
print(f"{sum('✅' in r for r in rows)} ✅ · {sum('🔶' in r for r in rows)} 🔶 · {sum('📝' in r for r in rows)} 📝 ({len(rows)} rows; {len(core_rows)} core-tier, {len(core_covered)} proven in every frontend)")
if bad: print("✅ without a test:", ", ".join(bad))
if missing: print("named tests that do not exist:", ", ".join(f"{a}→{b}" for a, b in missing))
if core_missing: print("core-tier ✅ without a CLI+window+tray proof in test_frontends_sync.py (AR-12):", ", ".join(core_missing))
sys.exit(1 if (bad or missing or core_missing) else 0)
