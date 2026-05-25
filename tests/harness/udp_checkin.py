"""
Wait for a umod4 device check-in UDP packet.

The WP sends one of two packet types to port 8081 when it connects to WiFi:
  - discover broadcast  {"type":"discover","device_name":"...","device_mac":"...","ip":"..."}
    sent to 255.255.255.255 when no server address is stored in flash.
  - checkin unicast     {"device_mac":"...","ip":"..."}
    sent to a specific server IP when one is stored in flash.

Binding to 0.0.0.0 with SO_BROADCAST captures both.

Usage:
    from harness.udp_checkin import wait_for_checkin, CheckInError

    ip = wait_for_checkin(timeout=60.0)
"""

import json
import socket

CHECKIN_PORT = 8081


class CheckInError(Exception):
    pass


def wait_for_checkin(timeout=60.0, port=CHECKIN_PORT):
    """
    Block until a device check-in packet arrives on UDP port 8081.
    Returns the device's IP address string.
    Raises CheckInError on timeout or malformed packet.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)
    try:
        sock.bind(("", port))
        data, addr = sock.recvfrom(1024)
    except socket.timeout:
        raise CheckInError(f"No check-in received within {timeout:.0f}s on UDP port {port}")
    finally:
        sock.close()

    try:
        payload = json.loads(data.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise CheckInError(f"Malformed check-in packet from {addr}: {e}")

    ip = payload.get("ip")
    if not ip:
        raise CheckInError(f"Check-in packet missing 'ip' field: {payload}")

    return ip
