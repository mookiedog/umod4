"""
WiFi connectivity test suite.

Phase 1: Verify WP is connected to WiFi (via RTT VFY channel).
          This also retrieves the WP IP address for subsequent HTTP tests.
Phase 2: Verify WP's HTTP server is reachable and returning valid responses.
"""

import json
import time
import urllib.request
import urllib.error

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL = 1
WIFI_TIMEOUT   = 10.0
HTTP_TIMEOUT   = 10.0

# How long to keep retrying while WP hasn't connected to WiFi yet.
WIFI_CONNECT_WAIT = 30.0
# How long to keep retrying while the HTTP server isn't listening yet.
HTTP_SERVER_WAIT  = 15.0


def _parse_kv(line):
    """Parse 'key=value' pairs from a VFY reply line into a dict."""
    kv = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            kv[k] = v.strip('"')
    return kv


def _http_get_json(ip, path, timeout=HTTP_TIMEOUT):
    """
    GET http://<ip><path> and return the parsed JSON dict.
    Raises urllib.error.URLError or json.JSONDecodeError on failure.
    """
    url = f"http://{ip}{path}"
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        body = resp.read().decode()
    return json.loads(body)


def _http_get_json_with_retry(ip, path, wait=HTTP_SERVER_WAIT):
    """
    Retry _http_get_json until it succeeds or wait seconds elapse.
    Only retries on ConnectionRefusedError (HTTP server not up yet).
    """
    deadline = time.monotonic() + wait
    last_err  = None
    while True:
        try:
            return _http_get_json(ip, path)
        except urllib.error.URLError as e:
            last_err = e
            if "Connection refused" not in str(e) or time.monotonic() >= deadline:
                raise
            time.sleep(1.0)


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # Phase 1 — WiFi status via RTT
        # Retry while WP reports not_connected (WiFi association in progress).
        # ----------------------------------------------------------------

        results.start("wifi_status")
        wp_ip   = None
        reply   = None
        deadline = time.monotonic() + WIFI_CONNECT_WAIT
        while True:
            try:
                reply = vfy.command("wifi_status", timeout=WIFI_TIMEOUT)
            except RttError as e:
                results.failed("wifi_status", str(e))
                return

            if "PASS" in reply:
                break
            if "not_connected" not in reply or time.monotonic() >= deadline:
                results.failed("wifi_status", reply)
                print("  NOTE: WP must be connected to WiFi for HTTP tests.")
                return
            time.sleep(2.0)

        kv    = _parse_kv(reply)
        wp_ip = kv.get("ip")
        if not wp_ip:
            results.failed("wifi_status", "No IP address in reply")
            return
        context["wp_ip"] = wp_ip
        results.passed("wifi_status", reply)

        # ----------------------------------------------------------------
        # Phase 2 — HTTP server reachability and response validation
        # Retry on ConnectionRefused while the HTTP server is starting up.
        # ----------------------------------------------------------------

        results.start("http_status")
        try:
            data = _http_get_json_with_retry(wp_ip, "/api/info")
            required = {"device_name", "wifi_connected", "wifi_ssid", "fs_status"}
            missing  = required - data.keys()
            if missing:
                results.failed("http_status", f"missing keys: {sorted(missing)}")
            elif not data.get("wifi_connected"):
                results.failed("http_status", "wifi_connected is false")
            else:
                results.passed("http_status",
                    f"device={data.get('device_name','')} "
                    f"fs={data.get('fs_status','')}")
        except (urllib.error.URLError, OSError) as e:
            results.failed("http_status", f"HTTP error: {e}")
        except json.JSONDecodeError as e:
            results.failed("http_status", f"JSON parse error: {e}")

        results.start("http_sd_info")
        try:
            data = _http_get_json_with_retry(wp_ip, "/api/sd-info")
            if "error" in data:
                results.failed("http_sd_info", f"not mounted: {data['error']}")
            else:
                required = {"total_mb", "used_mb", "files"}
                missing  = required - data.keys()
                if missing:
                    results.failed("http_sd_info", f"missing keys: {sorted(missing)}")
                else:
                    results.passed("http_sd_info",
                        f"total={data['total_mb']}MB used={data['used_mb']}MB")
        except (urllib.error.URLError, OSError) as e:
            results.failed("http_sd_info", f"HTTP error: {e}")
        except json.JSONDecodeError as e:
            results.failed("http_sd_info", f"JSON parse error: {e}")
