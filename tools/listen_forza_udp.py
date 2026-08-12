#!/usr/bin/env python3
"""Decode the Forza UDP telemetry this plugin emits, without needing SimHub.

Binds 127.0.0.1:8000 (the port the emitter targets, chosen because MOZA Pit
House listens there) and prints the fields that matter, so telemetry can be
verified independently of whatever is consuming it.

Field offsets are Forza Motorsport's "Data Out" dash layout, matching the
ForzaDashPacket struct in src/hooks_dinputffb.cpp.

Usage:  python tools/listen_forza_udp.py [seconds]
"""
import socket, struct, sys, time

PORT = 8000
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 20

# offset -> (label, struct format)
FIELDS = {
    0:   ("IsRaceOn",  "<i"),
    16:  ("RPM",       "<f"),
    244: ("Speed",     "<f"),
    303: ("Accel",     "<B"),
    304: ("Brake",     "<B"),
    306: ("HandBrake", "<B"),
    307: ("Gear",      "<B"),
    308: ("Steer",     "<b"),
}


def read(buf, off, fmt):
    size = struct.calcsize(fmt)
    if off + size > len(buf):
        return None
    return struct.unpack_from(fmt, buf, off)[0]


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    sock.bind(("127.0.0.1", PORT))
except OSError as e:
    print(f"  cannot bind 127.0.0.1:{PORT} -- something else has it "
          f"(MOZA Pit House or SimHub?): {e}")
    sys.exit(1)
sock.settimeout(1.0)

print(f"  listening on 127.0.0.1:{PORT} for {DURATION}s -- drive the car\n")
print(f"  {'time':>5}  {'race':>4} {'speed kph':>9} {'rpm':>6} {'gear':>4} "
      f"{'accel':>5} {'brake':>5} {'steer':>5}")

start = time.time()
n = 0
peak = {"Accel": 0, "Brake": 0}
last_print = 0.0
while time.time() - start < DURATION:
    try:
        data, _ = sock.recvfrom(2048)
    except socket.timeout:
        continue
    n += 1
    vals = {name: read(data, off, fmt) for off, (name, fmt) in FIELDS.items()}
    for k in peak:
        if vals.get(k) is not None:
            peak[k] = max(peak[k], vals[k])
    now = time.time() - start
    if now - last_print >= 0.5:          # 2 Hz is plenty to read
        last_print = now
        speed_kph = (vals["Speed"] or 0.0) * 3.6
        print(f"  {now:5.1f}  {vals['IsRaceOn']:>4} {speed_kph:9.1f} "
              f"{vals['RPM'] or 0:6.0f} {vals['Gear']:>4} "
              f"{vals['Accel']:>5} {vals['Brake']:>5} {vals['Steer']:>5}")

print(f"\n  packets received: {n}")
if not n:
    print("  NOTHING RECEIVED -- is [Telemetry] Enable = true, and the game in a race?")
else:
    print(f"  peak throttle: {peak['Accel']}/255      peak brake: {peak['Brake']}/255")
    if peak["Brake"] == 0:
        print("  brake never moved -- press the brake pedal during the capture window")
    else:
        print("  brake telemetry CONFIRMED -- SimHub will surface this as GameData.Brake")
