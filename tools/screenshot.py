#!/usr/bin/env python3
"""Grab screenshots from the running etube_ui firmware over USB serial.

usage: screenshot.py PORT OUTDIR [name[@x,y][!cmd][~secs] ...]
  Each name is saved as OUTDIR/name.png. "@x,y" first taps that point
  (serial fake tap); "!cmd" sends a serial command (e.g. !r6 plays station 6);
  "~secs" waits that long before the shot (default 1.5 s after a tap/cmd).
"""
import sys, time, zlib, struct, serial

def png(path, w, h, rgb565):
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            v = rgb565[2 * (y * w + x)] | rgb565[2 * (y * w + x) + 1] << 8
            r, g, b = v >> 11, (v >> 5) & 63, v & 31
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))

def grab(s):
    s.reset_input_buffer()
    s.write(b"p")
    deadline = time.time() + 10
    line = b""
    while not line.startswith(b"SHOT "):
        line = s.readline()
        if time.time() > deadline: raise TimeoutError("no SHOT header")
    w, h = map(int, line.split()[1:3])
    data = s.read(w * h * 2)
    if len(data) != w * h * 2: raise IOError(f"short read {len(data)}")
    return w, h, data

port, out, *shots = sys.argv[1:]
with serial.Serial(port, 115200, timeout=5) as s:
    time.sleep(0.5)
    for spec in shots:
        spec, _, wait = spec.partition("~")
        spec, _, cmd = spec.partition("!")
        name, _, tap = spec.partition("@")
        if tap: s.write(f"t{tap}\n".encode())
        if cmd: time.sleep(0.5); s.write(f"{cmd}\n".encode())
        if tap or cmd or wait: time.sleep(float(wait or 1.5))
        w, h, data = grab(s)
        png(f"{out}/{name}.png", w, h, data)
        print("saved", name)
