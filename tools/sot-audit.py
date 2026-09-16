#!/usr/bin/env python3
"""VF-2 audit: every ✅ row in docs/spec/requirements.md must name at least one existing test_* (or be a documented
rule/decision row that names its enforcement). Exit 1 on violations — wired into ctest."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
sot = (root / "docs/spec/requirements.md").read_text()
have = set()
for f in (root / "tests/integration").glob("*.py"):
    have |= set(re.findall(r"def (test_[a-z0-9_]+)", f.read_text()))
bad, missing = [], []
for line in sot.splitlines():
    m = re.match(r"\| ([A-Z]{2}-\d+) \|", line)
    if not m or "✅" not in line: continue
    rid = m.group(1)
    refs = re.findall(r"test_[a-z0-9_]+", line)
    if not refs and not re.search(r"enforced by|ctest|rule;", line): bad.append(rid); continue
    for r in refs:
        if r not in have and not any(h.startswith(r) for h in have): missing.append((rid, r))
rows = [l for l in sot.splitlines() if re.match(r"\| [A-Z]{2}-\d+ \|", l)]
print(f"{sum('✅' in r for r in rows)} ✅ · {sum('🔶' in r for r in rows)} 🔶 · {sum('📝' in r for r in rows)} 📝 ({len(rows)} rows)")
if bad: print("✅ without a test:", ", ".join(bad))
if missing: print("named tests that do not exist:", ", ".join(f"{a}→{b}" for a, b in missing))
sys.exit(1 if (bad or missing) else 0)
