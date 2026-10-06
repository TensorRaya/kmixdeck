// SPDX-License-Identifier: GPL-3.0-or-later
// DV-34: validates tests/tools/realloc-guard.c before any test trusts it ("a probe you have not seen fire proves
// nothing"). A builder-like block grows past a 4096-byte step while a pointer into the old block is kept.
//   without the probe: the stale read returns the old byte (exit 0) — the bug is invisible
//   with the probe:    the block moved, the stale read faults (SIGSEGV)
#include <stdio.h>
#include <stdlib.h>
// The stale read below IS the test; GCC rightly flags it (-Wuse-after-free), so the warning is off for this file only.
#pragma GCC diagnostic ignored "-Wuse-after-free"
int main(void) {
    char *volatile none = NULL;              /* volatile: GCC folds a literal realloc(NULL, n) into malloc(n) */
    char *p = realloc(none, 4096);
    if (!p) return 2;
    p[100] = 'x';
    char *stale = p;
    p = realloc(p, 8192);                    /* the builder grows: under the probe the old pages become PROT_NONE */
    if (!p) return 2;
    printf("moved=%d copy=%c\n", p != stale, p[100]);
    fflush(stdout);
    printf("stale read=%c\n", stale[100]);   /* faults under the probe */
    return 0;
}
