#!/usr/bin/env python3
"""Simulate BPSK frames as the comparator would see them and check decode().

Run: python3 tools/test_decode.py
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from etube_decode import decode  # noqa: E402


def pulses(bits, half_ns=500):
    """Comparator pulse widths for absolute BPSK, 2 carrier cycles per bit."""
    halves = []
    for b in bits:
        p = 1 if b else -1
        halves += [p, -p, p, -p]
    widths, cur, n = [], halves[0], 0
    for h in halves:
        if h == cur:
            n += 1
        else:
            widths.append(n * half_ns)
            cur, n = h, 1
    widths.append(n * half_ns)
    return widths


def main(frames=20000, bits=41, jitter_ns=150):
    random.seed(1)
    ok = 0
    for _ in range(frames):
        b = [1] + [random.randint(0, 1) for _ in range(bits - 1)]
        w = [x + random.uniform(-jitter_ns, jitter_ns) for x in pulses(b)]
        bpsk, dbpsk = decode(w)
        expect_dbpsk = [0] + [int(b[k] != b[k - 1]) for k in range(1, len(b))]
        ok += bpsk == b and dbpsk == expect_dbpsk
    print(f"{ok}/{frames} frames decoded correctly with ±{jitter_ns} ns jitter")
    return 0 if ok == frames else 1


if __name__ == "__main__":
    sys.exit(main())
