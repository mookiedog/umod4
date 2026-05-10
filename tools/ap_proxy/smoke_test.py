#!/usr/bin/env python3
"""
Smoke test for ap_proxy firmware.

Usage:
    build/.venv/bin/python3 tools/ap_proxy/smoke_test.py [/dev/ttyACM0]

With no argument, scans all /dev/ttyACM* ports and uses the first that responds.
Tests PING, STATUS, and SCAN — no umod4 device needed.
If a umod4_XXXX network is visible it will also test CONNECT and GET /api/info.
"""

import glob
import sys
import time
import serial

BAUD = 115200


def try_ping(port, timeout=2.0):
    """Open port and send PING. Returns True if the proxy pico answers."""
    try:
        with serial.Serial(port, BAUD, timeout=1.0) as ser:
            time.sleep(0.3)
            ser.reset_input_buffer()
            ser.write(b"PING\n")
            ser.flush()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                raw = ser.readline()
                if raw and raw.strip().startswith(b"OK"):
                    return True
        return False
    except (serial.SerialException, OSError):
        return False


def find_port():
    candidates = sorted(glob.glob("/dev/ttyACM*"))
    if not candidates:
        print("No /dev/ttyACM* devices found. Is the proxy Pico attached and enumerated?")
        sys.exit(1)
    print(f"Found ports: {candidates}")
    for port in candidates:
        print(f"  Trying {port}... ", end="", flush=True)
        if try_ping(port):
            print("ap_proxy responded")
            return port
        print("no response")
    print("ERROR: ap_proxy did not respond on any port.")
    print("  - Check usbipd attach for the proxy Pico's serial port")
    print("  - Try re-flashing if this is the first boot after programming")
    sys.exit(1)


def cmd(ser, line, timeout=20.0):
    ser.reset_input_buffer()
    ser.write((line + "\n").encode())
    ser.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if raw:
            resp = raw.decode(errors="replace").strip()
            if resp.startswith("OK") or resp.startswith("ERR"):
                return resp
    raise TimeoutError(f"No response to: {line!r}")


def check(label, resp, expect_prefix="OK"):
    ok = resp.startswith(expect_prefix)
    status = "PASS" if ok else "FAIL"
    print(f"  {status}  {label}: {resp}")
    return ok


if len(sys.argv) > 1:
    PORT = sys.argv[1]
    print(f"Using specified port: {PORT}")
else:
    PORT = find_port()

print(f"\nRunning smoke test on {PORT}")
with serial.Serial(PORT, BAUD, timeout=1.0) as ser:
    time.sleep(0.3)
    ser.reset_input_buffer()

    all_passed = True

    print("\n--- Basic connectivity ---")
    all_passed &= check("PING",   cmd(ser, "PING",   timeout=3.0))
    all_passed &= check("STATUS", cmd(ser, "STATUS", timeout=5.0))

    print("\n--- WiFi scan (up to 8s) ---")
    resp = cmd(ser, "SCAN", timeout=12.0)
    all_passed &= check("SCAN", resp)
    ssids = resp.split()[1:] if resp.startswith("OK") else []
    print(f"       found {len(ssids)} umod4 network(s): {ssids}")

    if ssids:
        ssid = ssids[0]
        print(f"\n--- Connecting to {ssid} ---")
        resp = cmd(ser, f"CONNECT {ssid}", timeout=30.0)
        if check("CONNECT", resp):
            ip = resp.split()[2] if len(resp.split()) >= 3 else "?"
            print(f"       assigned IP: {ip}")

            print("\n--- HTTP GET /api/info ---")
            resp = cmd(ser, "GET /api/info", timeout=10.0)
            all_passed &= check("GET /api/info", resp)

            print("\n--- Disconnect ---")
            resp = cmd(ser, "DISCONNECT", timeout=5.0)
            check("DISCONNECT", resp)
        else:
            all_passed = False

    print()
    print("PASSED" if all_passed else "FAILED")
