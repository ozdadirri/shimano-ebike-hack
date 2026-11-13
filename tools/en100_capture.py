#!/usr/bin/env python3
"""Capture EW-EN100 notifications with firmware/ble_explorer running on the ESP32-S3.

Resets the board, rescans until the bike shows up (by name, or anything the sketch marks
"<== Shimano"), connects to it, and prints everything for DURATION seconds after connecting.
Switch the bike on AFTER starting this: the EN100 only advertises while nothing is connected.

usage: python3 en100_capture.py DURATION > capture.txt     (pip3 install pyserial)
"""
import serial, sys, time, re, glob
p = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.2)
p.dtr = False; p.rts = True; time.sleep(0.1); p.rts = False
buf = ''; t0 = time.time(); sent = False; dur = float(sys.argv[1]); scans = 0
while time.time() - t0 < dur:
    d = p.read(4096).decode('utf-8', 'replace')
    if d: buf += d; sys.stdout.write(d); sys.stdout.flush()
    if not sent and 'Scan done' in buf:
        m = re.search(r'\[ *(\d+)\] \S+ .*(Borghini|<== Shimano)', buf)
        if m:
            p.write((m.group(1) + '\n').encode()); print('\n>>> connect', m.group(1)); sent = True; t0 = time.time()
        else:
            scans += 1
            if scans >= 18: print('\n>>> EN100 not found after 18 scans'); break
            print('\n>>> not found, rescanning'); buf = ''; p.write(b's\n')
