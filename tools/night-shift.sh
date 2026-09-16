#!/usr/bin/env bash
# kmixdeck night shift 2026-09-16 → 17. Autonomous, resumable. Every block: build → suite → commit+push only on green.
# Progress in /tmp/kmixdeck-night/, report in /tmp/kmixdeck-night/REPORT.md. Re-running skips finished blocks.
set -u
R=~/repos/kmixdeck; N=/tmp/kmixdeck-night; mkdir -p $N; cd $R
log(){ echo "$(date +%H:%M:%S) $*" | tee -a $N/log.txt; }
suite(){ # full ctest, serialized; returns 0 on 100 %
  local tag=$1; (cd build && ctest --output-on-failure > $N/ctest-$tag.log 2>&1); grep -q "100% tests passed" $N/ctest-$tag.log; }
build(){ ninja -C build > $N/build.log 2>&1; }
green_commit(){ # $1 tag, $2 message
  if ! build; then log "BUILD FAILED ($1)"; grep -m3 "error" $N/build.log | tee -a $N/log.txt; return 1; fi
  if suite $1; then git add -A && git commit -qm "$2" && git push -q origin main && log "GREEN + pushed: $1 ($(git log --oneline -1 | cut -c1-7))"; touch $N/done-$1; return 0
  else log "SUITE RED ($1) — kept in tree, not committed"; grep -E "^FAILED|^E  +Assert" $N/ctest-$tag.log 2>/dev/null | head -5 | tee -a $N/log.txt
       grep -E "^FAILED|^E  +Assert" $N/ctest-$1.log | head -5 | tee -a $N/log.txt; return 1; fi
}
sot_count(){ python3 tools/sot-audit.py 2>/dev/null | head -1; }

log "=== night shift start, HEAD $(git log --oneline -1 | cut -c1-7), $(sot_count)"

# ---- Block 0: the UX-11 fix that ctest20 ran without (binary was stale) — must be green before anything else
if [ ! -f $N/done-b0 ]; then log "B0 verify HEAD"; if build && suite b0; then touch $N/done-b0; log "B0 HEAD green"; else log "B0 HEAD RED — stop, humans needed"; grep -E "^FAILED|^E  +Assert" $N/ctest-b0.log | head -5 | tee -a $N/log.txt; fi; fi

# ---- Block 1..N are python scripts in tools/night/ that patch the tree; each idempotent
for step in tools/night/[0-9]*.py; do
  [ -f "$step" ] || continue
  tag=$(basename $step .py)
  [ -f $N/done-$tag ] && continue
  [ -f $N/done-b0 ] || { log "skipping $tag: base not green"; continue; }
  log "--- $tag start"
  if python3 $step >> $N/log.txt 2>&1; then
    msg=$(python3 $step --message 2>/dev/null || echo "night: $tag")
    green_commit $tag "$msg" || {   # never throw work away: park it on a branch, main stays clean
      br="night/$tag-$(date +%H%M)"; git checkout -q -b "$br" && git add -A && git commit -qm "WIP (suite red): $msg" && git push -q -u origin "$br"
      log "$tag RED → parked on $br"; git checkout -q main; }
  else log "$tag script error"; br="night/$tag-err-$(date +%H%M)"; git checkout -q -b "$br" && git add -A && git commit -qm "WIP (script error): $tag" && git push -q -u origin "$br"; git checkout -q main; fi
done

# ---- Report
{
  echo "# kmixdeck Nachtschicht $(date +%F)"; echo
  echo "HEAD: \`$(git log --oneline -1 | cut -c1-60)\`  ·  SoT: $(sot_count)"; echo
  echo "## Erledigt (grün + gepusht)"; for f in $N/done-*; do echo "- $(basename $f | sed 's/done-//')"; done; echo
  echo "## Nicht geschafft"; grep -E "RED|FAILED|error" $N/log.txt | tail -20; echo
  echo "## Commits der Nacht"; git log --since="$(date -d '-14 hours' '+%F %H:%M')" --oneline | cut -c1-100; echo
  echo "## Letzter Suite-Lauf"; ls -t $N/ctest-*.log | head -1 | xargs grep -E "tests passed"; 
} > $N/REPORT.md
log "=== night shift end"
