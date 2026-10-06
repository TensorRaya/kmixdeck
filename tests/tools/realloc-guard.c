// SPDX-License-Identifier: GPL-3.0-or-later
// DV-34 / ADR 0015: the probe that makes PipeWire #5202 deterministic. Preloaded into ONE process (LD_PRELOAD), it turns
// every realloc() to a page-multiple size — spa_pod_dynamic_builder grows in 4096-byte steps — into "move to a fresh
// mmap and make the OLD pages PROT_NONE" instead of a free. A pointer kept across that realloc (the dereffed params in
// filter-chain's setup_streams() on 1.6.0-1.6.2) then faults with SIGSEGV on its first read. Without the probe the
// freed bytes are usually still intact and the bug shows up only now and then, as garbage (CI runs 37378915814,
// 37391578286). free() of a guarded block also leaves it PROT_NONE, so the pages are never handed out again.
// Only for tests: every guarded block costs a mapping that is never returned.
#ifndef _GNU_SOURCE   // KDE's compiler settings already pass -D_GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);

// The whole point is to replace libc's realloc/free in the preloaded process, so both must be exported whatever the
// build's default visibility is (KDECompilerSettings sets hidden: the first build exported nothing and the selftest
// stayed at moved=0 — measured 2026-10-06).
#define GUARD_EXPORT __attribute__((visibility("default")))

#define MAXB 16384
static struct { void *p; size_t n, map; } reg[MAXB];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static int find(const void *p) {
    for (int i = 0; i < MAXB; i++)
        if (reg[i].p == p) return i;
    return -1;
}
// Only page-aligned pointers can be ours (mmap). glibc chunks are 16-aligned and almost never page-aligned, so the hot
// free() path skips the table scan.
static int ours(const void *p) { return p && ((uintptr_t)p & 4095) == 0; }

GUARD_EXPORT void *realloc(void *p, size_t n) {
    pthread_mutex_lock(&mu);
    const int i = ours(p) ? find(p) : -1;
    if (i < 0 && (n == 0 || n % 4096 != 0)) { pthread_mutex_unlock(&mu); return __libc_realloc(p, n); }
    const size_t map = (n + 4095) & ~(size_t)4095;
    void *q = mmap(NULL, map, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (q == MAP_FAILED) { pthread_mutex_unlock(&mu); return NULL; }
    if (p) {
        const size_t old = i >= 0 ? reg[i].n : malloc_usable_size(p);
        memcpy(q, p, old < n ? old : n);
        if (i >= 0) { mprotect(reg[i].p, reg[i].map, PROT_NONE); reg[i].p = NULL; }
        else __libc_free(p);
    }
    const int j = find(NULL);
    // An unregistered mmap block would later reach __libc_free and crash somewhere unrelated: fail loudly here instead.
    if (j < 0) { fputs("realloc-guard: table full\n", stderr); abort(); }
    reg[j].p = q; reg[j].n = n; reg[j].map = map;
    pthread_mutex_unlock(&mu);
    return q;
}

GUARD_EXPORT void free(void *p) {
    if (!p) return;
    if (!ours(p)) { __libc_free(p); return; }
    pthread_mutex_lock(&mu);
    const int i = find(p);
    if (i >= 0) { mprotect(reg[i].p, reg[i].map, PROT_NONE); reg[i].p = NULL; pthread_mutex_unlock(&mu); return; }
    pthread_mutex_unlock(&mu);
    __libc_free(p);
}
