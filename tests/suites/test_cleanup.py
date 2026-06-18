"""
Filesystem cleanup test suite.

Deletes transient files (.um4 logs, .uf2 uploads) from the WP's SD card
to prevent LittleFS block-allocator degradation across nightly runs.

Must run after test_wifi (needs wp_ip in context) and before OTA suites.
"""

import os
import sys

_SERVER_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "../../tools/server"))
sys.path.insert(0, _SERVER_DIR)


DELETABLE_EXTENSIONS = {".um4", ".uf2"}


def run(ocd, results, context):
    try:
        from device_client import DeviceClient
    except ImportError as e:
        results.abort("cleanup_list",
            f"DeviceClient import failed: {e} — run with build/.venv/bin/python3")

    wp_ip = context.get("wp_ip")
    if not wp_ip:
        results.abort("cleanup_list", "no wp_ip in context — test_wifi must run first")

    client = DeviceClient(wp_ip)

    # ----------------------------------------------------------------
    # cleanup_list — get file listing from device
    # ----------------------------------------------------------------

    results.start("cleanup_list")
    files = client.list_log_files()
    if files is None:
        results.abort("cleanup_list", "failed to list files from device")

    to_delete = []
    for f in files:
        name = f.get("name", "")
        _, ext = os.path.splitext(name)
        if ext.lower() in DELETABLE_EXTENSIONS:
            to_delete.append(name)

    results.passed("cleanup_list",
        f"{len(to_delete)} deletable of {len(files)} files")

    if not to_delete:
        return

    # ----------------------------------------------------------------
    # cleanup_delete — delete each transient file
    # ----------------------------------------------------------------

    results.start("cleanup_delete")
    deleted = 0
    errors = []
    for name in to_delete:
        ok, err = client.delete_log_file(name)
        if ok:
            deleted += 1
            print(f"  deleted {name}")
        else:
            errors.append(f"{name}: {err}")

    if errors:
        results.failed("cleanup_delete",
            f"deleted {deleted}/{len(to_delete)}, errors: {'; '.join(errors)}")
    else:
        results.passed("cleanup_delete", f"deleted {deleted} files")
