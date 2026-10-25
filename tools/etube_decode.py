#!/usr/bin/env python3
"""etube_decode.py: read 'F n w0 w1 ...' lines from the ESP32 and print bits.

Usage: python3 etube_decode.py /dev/cu.usbmodem1101 [label]
Each run appends to <label>.log so experiments can be compared side by side.
"""
import sys
import collections

HALF_NS = 500          # one carrier half-cycle (1 MHz carrier)


def decode(widths_ns):
    """Return (bpsk_bits, dbpsk_bits). Time is counted in whole half-cycles,
    so per-pulse timing errors cannot add up across a long frame."""
    units, flips = 0, set()
    for w in widths_ns:
        n = round(w / HALF_NS)
        if n == 2:                         # 1 us pulse = phase flip at its centre
            flips.add((units + 1) // 4)    # 4 half-cycles per bit
        units += n
    nbits = units // 4
    bpsk, cur = [], 1                      # first bit is unknown; assume 1
    for k in range(nbits):
        if k in flips:
            cur ^= 1
        bpsk.append(cur)
    dbpsk = [1 if k in flips else 0 for k in range(nbits)]
    return bpsk, dbpsk


def main():
    import serial  # pyserial; imported here so decode() can be tested without it

    port = serial.Serial(sys.argv[1], 115200, timeout=1)
    label = sys.argv[2] if len(sys.argv) > 2 else "capture"
    seen = collections.Counter()
    with open(f"{label}.log", "a") as log:
        for raw in port:
            parts = raw.decode(errors="ignore").split()
            if len(parts) < 3 or parts[0] != "F":
                continue
            widths = [int(x) for x in parts[2:] if x != "0"]
            bpsk, dbpsk = decode(widths)
            a = "".join(map(str, bpsk))
            b = "".join(map(str, dbpsk))
            seen[a] += 1
            line = f"{len(bpsk):4d}  BPSK {a}\n      DBPSK {b}"
            print(line + ("" if seen[a] > 1 else "   <-- new"))
            log.write(f"{label}\t{a}\t{b}\n")


if __name__ == "__main__":
    main()
