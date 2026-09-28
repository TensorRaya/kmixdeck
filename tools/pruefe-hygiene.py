#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""Repository hygiene the compiler does not see (code review v0.5, BP-7).

1. Persistence: every file the product writes goes through QSaveFile (write next to the target, rename).
   A bare QFile opened WriteOnly leaves a half-written scene/export behind when the process dies mid-write.
2. No internal references in shipped code: no person names in comments, no developer home paths,
   no scratch directories of a particular machine.

Exit 1 with one line per finding; exit 0 prints "ok".
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CODE = ["src", "web", "streamdeck", "interfaces", "data", "tools", "tests"]
EXT = {".cpp", ".h", ".qml", ".py", ".js", ".html", ".css", ".xml", ".conf", ".in", ".json", ""}
SELF = pathlib.Path(__file__).resolve()

RULES = [
    ("QFile opened for writing — use QSaveFile (BP-7)", re.compile(r"\bQFile\b[^;]*;\s*(?:[^\n]*\n){0,2}?[^\n]*\.open\([^)]*WriteOnly")),
    ("person name in shipped code", re.compile(r"\bMichel\b")),
    ("developer home path", re.compile(r"/home/[a-z]")),
    ("machine-specific scratch dir", re.compile(r"/var/tmp/(?:ab-|kmx-)")),
]

def files():
    for top in CODE:
        for p in (ROOT / top).rglob("*"):
            if p.is_file() and p.resolve() != SELF and "__pycache__" not in p.parts and p.suffix in EXT:
                yield p

findings = []
for p in files():
    try: text = p.read_text(encoding="utf-8")
    except UnicodeDecodeError: continue
    shipped = p.relative_to(ROOT).parts[0] in ("src", "web", "streamdeck")
    for why, rx in RULES:
        if why.startswith("QFile") and not shipped: continue   # test fixtures may write however they like
        for m in rx.finditer(text):
            findings.append(f"{p.relative_to(ROOT)}:{text.count(chr(10), 0, m.start()) + 1}: {why}")

for f in sorted(findings): print(f)
print("ok" if not findings else f"{len(findings)} finding(s)")
sys.exit(1 if findings else 0)
