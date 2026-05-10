"""
ap_proxy USB VID:PID — loaded from tools/ap_proxy/usb_ids.txt.

All test-harness code that needs the ap_proxy VID, PID, or "VID:PID" string
should import from here rather than hardcoding values.
"""

import os

_PROJ_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
_USB_IDS_PATH = os.path.join(_PROJ_ROOT, "tools", "ap_proxy", "usb_ids.txt")


def _load(path):
    ids = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                k, _, v = line.partition('=')
                ids[k.strip()] = v.strip()
    return ids


_ids = _load(_USB_IDS_PATH)

AP_PROXY_VID    = int(_ids["AP_PROXY_VID"], 16)
AP_PROXY_PID    = int(_ids["AP_PROXY_PID"], 16)
AP_PROXY_HW_ID  = f"{_ids['AP_PROXY_VID'].lower()}:{_ids['AP_PROXY_PID'].lower()}"
