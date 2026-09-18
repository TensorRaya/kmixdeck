# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""Waiting for observables — the one helper every suite uses (review 2026-09-18, IT-1).

Rule from CONTRIBUTING: wait for the observable, never for a clock. These helpers make that the short way to write it,
and when the observable never arrives they say WHAT was seen last instead of leaving the next assert to fail with
an unrelated message.
"""
import time


class Timeout(AssertionError):
    pass


def wait_for(pred, timeout=5.0, dt=0.1, what="condition"):
    """Poll `pred()` until truthy; return its value. Raise Timeout naming `what` and the last value seen."""
    deadline = time.monotonic() + timeout
    last = None
    while True:
        last = pred()
        if last: return last
        if time.monotonic() >= deadline: raise Timeout(f"{what} did not happen within {timeout:.1f}s (last: {last!r})")
        time.sleep(dt)


def wait_eq(fn, want, timeout=5.0, dt=0.1, what=None):
    """Poll `fn()` until it equals `want`; return it. Timeout message shows want vs last."""
    deadline = time.monotonic() + timeout
    while True:
        got = fn()
        if got == want: return got
        if time.monotonic() >= deadline:
            raise Timeout(f"{what or 'value'}: wanted {want!r}, last seen {got!r} after {timeout:.1f}s")
        time.sleep(dt)


def wait_level(fn, pred, tries=6, what="level"):
    """Repeat an audio MEASUREMENT (each call records ~1.5 s) until `pred(level)`; return the last level.
    Not time-based: a measurement is the clock. Returns the last value even on failure so the caller can assert with it."""
    v = None
    for _ in range(tries):
        v = fn()
        if pred(v): return v
    return v


def settle(seconds):
    """A deliberate, documented pause where no observable exists (e.g. letting WirePlumber apply a batch before the NEXT
    action). Prefer wait_for; use this only with a reason in the call site. Kept as a function so `grep settle` finds
    every one of them."""
    time.sleep(seconds)
