"""
Flash region backup and restore for umod4 devices.

Backup:  GET /api/flash/<region> over HTTP (device must be in STA mode).
Restore: WP config → OpenOCD direct flash write (device halted via CMSIS-DAP).
         EP image-store → not yet implemented (needs a POST firmware endpoint).

Backup file format
------------------
  [4 bytes]  magic   b'UMB4'
  [1 byte]   version 0x01
  [2 bytes]  record count (little-endian)
  per record:
    [1 byte]   proc     0=WP, 1=EP
    [4 bytes]  address  absolute flash address (little-endian)
    [4 bytes]  length   byte count (little-endian)
    [N bytes]  data

Addresses use the XIP base so they are directly usable by OpenOCD program.
  WP config:       0x103FE000  (XIP_BASE 0x10000000 + FLASH_CONFIG_OFFSET 0x3FE000)
  EP image-store:  0x10200000 + slot_index * 65536
"""

import os
import struct
import subprocess
import tempfile

import requests

# ---------------------------------------------------------------------------
# Constants — must match firmware headers

MAGIC   = b'UMB4'
VERSION = 0x01

PROC_WP = 0
PROC_EP = 1

# WP config partition
WP_FLASH_BASE    = 0x10000000
WP_CONFIG_OFFSET = 0x3FE000     # must match FlashConfig.h FLASH_CONFIG_OFFSET
WP_CONFIG_SIZE   = 512          # sizeof(flash_config_t)
WP_FLASH_SECTOR  = 4096         # minimum OpenOCD erase unit

# EP image-store partition
EP_IMAGE_STORE_BASE  = 0x10200000   # must match ep_flash_layout.h
EP_IMAGE_STORE_SLOTS = 128
EP_SLOT_SIZE         = 65536

# Must match FLASH_READ_MAX_SIZE in fs_custom.cpp
HTTP_CHUNK_SIZE = 4096

# OpenOCD defaults
OPENOCD      = "/usr/local/bin/openocd"
OPENOCD_IFACE  = "interface/cmsis-dap.cfg"
OPENOCD_TARGET = "target/rp2350.cfg"


# ---------------------------------------------------------------------------
# Record

class Record:
    __slots__ = ('proc', 'address', 'data')

    def __init__(self, proc: int, address: int, data: bytes):
        self.proc    = proc
        self.address = address
        self.data    = data

    def __repr__(self):
        proc_name = "WP" if self.proc == PROC_WP else "EP"
        return f"Record({proc_name}, 0x{self.address:08X}, {len(self.data)} bytes)"


# ---------------------------------------------------------------------------
# HTTP helpers

def _flash_get(session: requests.Session, base_url: str,
               region: str, offset: int, length: int) -> bytes:
    url  = f"{base_url}/api/flash/{region}?offset={offset}&len={length}"
    resp = session.get(url, timeout=30)
    resp.raise_for_status()
    return resp.content


def _fetch_ep_slot(session: requests.Session, base_url: str, slot_idx: int) -> bytes:
    buf = bytearray()
    base_offset = slot_idx * EP_SLOT_SIZE
    while len(buf) < EP_SLOT_SIZE:
        chunk_offset = base_offset + len(buf)
        chunk_len    = min(HTTP_CHUNK_SIZE, EP_SLOT_SIZE - len(buf))
        chunk        = _flash_get(session, base_url, "ep-image-store",
                                  chunk_offset, chunk_len)
        if not chunk:
            break
        buf.extend(chunk)
    return bytes(buf)


# ---------------------------------------------------------------------------
# Backup

def backup_region(session: requests.Session, base_url: str,
                  region: str) -> list[Record]:
    """
    Download a flash region from the device and return a list of Records.
    Device must be in STA mode (HTTP reachable).
    """
    if region == "wp-config":
        data = _flash_get(session, base_url, "wp-config", 0, WP_CONFIG_SIZE)
        return [Record(PROC_WP, WP_FLASH_BASE + WP_CONFIG_OFFSET, data)]

    if region == "ep-image-store":
        # Identify occupied slots via the existing scan endpoint
        scan = session.get(f"{base_url}/api/image-store/scan", timeout=60)
        scan.raise_for_status()
        scan_slots = scan.json().get("slots", [])
        valid_indices = sorted(
            s["index"] for s in scan_slots
            if "index" in s and "error" not in s and s["index"] != 0
        )

        records = []

        # Slot 0 is the image_selector — include it if non-empty
        slot0 = _fetch_ep_slot(session, base_url, 0)
        if slot0[:4] != b'\xFF\xFF\xFF\xFF':
            records.append(Record(PROC_EP, EP_IMAGE_STORE_BASE, slot0))

        # Include each valid image slot
        for idx in valid_indices:
            data = _fetch_ep_slot(session, base_url, idx)
            addr = EP_IMAGE_STORE_BASE + idx * EP_SLOT_SIZE
            records.append(Record(PROC_EP, addr, data))

        return records

    raise ValueError(f"Unknown region: {region!r}")


# ---------------------------------------------------------------------------
# File I/O

def save_backup(records: list[Record], path: str) -> None:
    """Write records to a backup file."""
    with open(path, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<BH', VERSION, len(records)))
        for r in records:
            f.write(struct.pack('<BII', r.proc, r.address, len(r.data)))
            f.write(r.data)


def load_backup(path: str) -> list[Record]:
    """Read records from a backup file."""
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != MAGIC:
            raise ValueError(f"Bad magic in {path!r}: {magic!r}")
        version, count = struct.unpack('<BH', f.read(3))
        if version != VERSION:
            raise ValueError(f"Unsupported backup version {version}")
        records = []
        for _ in range(count):
            proc, address, length = struct.unpack('<BII', f.read(9))
            data = f.read(length)
            if len(data) != length:
                raise ValueError("Truncated backup file")
            records.append(Record(proc, address, data))
    return records


# ---------------------------------------------------------------------------
# Restore

def restore_wp_config(records: list[Record],
                      openocd: str = OPENOCD) -> None:
    """
    Write WP config record(s) directly to WP flash via OpenOCD.
    The CMSIS-DAP probe must be attached and WP must not be running user code
    that conflicts with flash programming (OpenOCD halts it automatically).
    """
    wp_records = [r for r in records if r.proc == PROC_WP]
    if not wp_records:
        raise ValueError("No WP records in backup")

    for r in wp_records:
        # Pad to sector size with 0xFF (erased-flash state) so OpenOCD programs
        # a complete sector without corrupting adjacent bytes.
        remainder = len(r.data) % WP_FLASH_SECTOR
        padded    = r.data if remainder == 0 else \
                    r.data + b'\xFF' * (WP_FLASH_SECTOR - remainder)

        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as tmp:
            tmp.write(padded)
            tmp_path = tmp.name

        try:
            tcl = (f"adapter speed 20000; init; halt; "
                   f"program {{{tmp_path}}} 0x{r.address:08X}; "
                   f"reset run; exit")
            result = subprocess.run(
                [openocd, '-f', OPENOCD_IFACE, '-f', OPENOCD_TARGET, '-c', tcl],
                timeout=30, capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError(
                    f"OpenOCD failed (rc={result.returncode}):\n{result.stderr}")
        finally:
            os.unlink(tmp_path)


def restore_ep_image_store(records: list[Record],
                            session: requests.Session,
                            base_url: str) -> None:
    """
    Restore EP image-store records.
    Not yet implemented — requires a POST /api/flash/ep-image-store firmware endpoint.
    """
    raise NotImplementedError(
        "EP image-store restore requires POST /api/flash/ep-image-store "
        "(firmware endpoint not yet implemented)")
