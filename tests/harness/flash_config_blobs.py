"""
flash_config_blobs.py — generate synthetic flash_config_t blobs for testing.

Each make_*() function returns a 512-byte bytes object that can be written
directly to the WP config flash partition via flash_ops.restore_wp_config().

Struct layout mirrors flash_config_v0_t in WP/src/FlashConfig.h exactly.
If the struct changes, bump the version and add a new make_valid_v1() etc.
"""

import struct
import zlib

# Must match FlashConfig.h
FLASH_CONFIG_MAGIC   = 0x55CF0000
FLASH_CONFIG_VERSION = 0
FLASH_CONFIG_SIZE    = 512
_CRC_DATA_LEN        = FLASH_CONFIG_SIZE - 4   # bytes covered by crc32

# v0 field sizes (packed, no padding)
_V0_FIELDS = [
    ('magic',        4),
    ('version',      4),
    ('device_name', 64),
    ('wifi_ssid',   64),
    ('wifi_password',64),
    ('ap_ssid',     32),
    ('ap_password', 64),
    ('_reserved',  212),
    ('crc32',        4),
]
assert sum(s for _, s in _V0_FIELDS) == FLASH_CONFIG_SIZE


def _crc32(data: bytes) -> int:
    """CRC32 using the Ethernet/zlib polynomial — matches firmware crc32_compute()."""
    return zlib.crc32(data) & 0xFFFFFFFF


def _pack_str(s: str, size: int) -> bytes:
    """Encode s as null-terminated, zero-padded to exactly size bytes."""
    b = s.encode('utf-8')[:size - 1]
    return b + b'\x00' * (size - len(b))


def _body_v0(magic: int, version: int,
             device_name: str, wifi_ssid: str, wifi_password: str,
             ap_ssid: str, ap_password: str) -> bytes:
    """Pack v0 fields without the trailing crc32."""
    body  = struct.pack('<II', magic, version)
    body += _pack_str(device_name,   64)
    body += _pack_str(wifi_ssid,     64)
    body += _pack_str(wifi_password, 64)
    body += _pack_str(ap_ssid,       32)
    body += _pack_str(ap_password,   64)
    body += b'\x00' * 212   # _reserved[53]
    assert len(body) == _CRC_DATA_LEN
    return body


# ---------------------------------------------------------------------------
# Public generators

def make_blank() -> bytes:
    """512 bytes of 0xFF — simulates erased flash (never been written)."""
    return b'\xFF' * FLASH_CONFIG_SIZE


def make_invalid_magic(wifi_ssid: str = '', wifi_password: str = '',
                       device_name: str = '', ap_ssid: str = '',
                       ap_password: str = '') -> bytes:
    """
    Valid v0 layout and CRC, but wrong magic.
    Firmware must detect the bad magic and apply clean defaults.
    """
    body = _body_v0(0xDEADBEEF, FLASH_CONFIG_VERSION,
                    device_name, wifi_ssid, wifi_password,
                    ap_ssid, ap_password)
    return body + struct.pack('<I', _crc32(body))


def make_bad_crc(wifi_ssid: str = '', wifi_password: str = '',
                 device_name: str = '', ap_ssid: str = '',
                 ap_password: str = '') -> bytes:
    """
    Correct magic, plausible field values, but deliberately corrupted CRC.
    Firmware must detect the bad CRC and apply clean defaults.
    """
    body = _body_v0(FLASH_CONFIG_MAGIC, FLASH_CONFIG_VERSION,
                    device_name, wifi_ssid, wifi_password,
                    ap_ssid, ap_password)
    bad_crc = _crc32(body) ^ 0xFFFFFFFF   # invert all bits
    return body + struct.pack('<I', bad_crc)


def make_valid_v0(wifi_ssid: str = '', wifi_password: str = '',
                  device_name: str = '', ap_ssid: str = '',
                  ap_password: str = '') -> bytes:
    """
    Fully valid v0 config blob with correct magic, version, and CRC.
    Firmware must load it without modification.
    """
    body = _body_v0(FLASH_CONFIG_MAGIC, FLASH_CONFIG_VERSION,
                    device_name, wifi_ssid, wifi_password,
                    ap_ssid, ap_password)
    return body + struct.pack('<I', _crc32(body))
