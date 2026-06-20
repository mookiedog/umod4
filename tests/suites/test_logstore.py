"""
LogStore integrity test suite.

Injects crafted .meta files with known errors, runs FSCK to verify
detection, then cleans up. Uses log_tNNN.meta naming so test files
are invisible to LogStore's boot-time init (strict log_DIGITS.meta
matching) and harmless if left behind after a crash.

Prerequisites: test_basic must have passed (SD + filesystem healthy).
"""

import json

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL = 1

# Test metadata files: (log_num, chunks_str, offset, total, expected_error_keyword)
# expected_error_keyword=None means the file is valid (control case)
TEST_CASES = [
    (900, "0",    1000,      1000,      "CHUNK0"),
    (901, "9999", 1000,      1000,      "RANGE"),
    (902, "900",  1000,      1000,      None),        # first claim of chunk 900
    (903, "900",  1000,      1000,      "DOUBLE"),    # second claim of chunk 900
    (904, "901",  999999999, 999999999, "OFFSET"),
    (905, "902",  1000,      9999,      "SIZE"),
    (906, "903",  1000,      1000,      None),        # valid control
]

EXPECTED_ERRORS = sum(1 for _, _, _, _, k in TEST_CASES if k is not None)


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # logstore_inject — write all test metadata files
        # ----------------------------------------------------------------

        results.start("logstore_inject")
        try:
            for log_num, chunks, offset, total, _ in TEST_CASES:
                cmd = f"logstore_write_test_meta {log_num} {chunks} {offset} {total}"
                reply = vfy.command(cmd, timeout=5.0)
                data = json.loads(reply)
                state = data.get("logstore_write_test_meta", {}).get("state", "")
                if state != "ok":
                    results.abort("logstore_inject", f"failed to write log_t{log_num}.meta: {reply}")
            results.passed("logstore_inject", f"{len(TEST_CASES)} test files written")
        except (RttError, json.JSONDecodeError) as e:
            results.abort("logstore_inject", str(e))

        # ----------------------------------------------------------------
        # logstore_fsck_errors — run FSCK, expect exactly EXPECTED_ERRORS
        # ----------------------------------------------------------------

        results.start("logstore_fsck_errors")
        try:
            # Collect error messages that arrive before the summary
            error_msgs = []
            reply = vfy.command("logstore_fsck", timeout=30.0)

            # The VFY channel may have buffered logstore_fsck_error lines
            # before the final logstore_fsck summary. Drain them.
            data = json.loads(reply)
            fsck = data.get("logstore_fsck", {})

            # If the first reply was an error line, keep reading until summary
            while "logstore_fsck_error" in data:
                error_msgs.append(data["logstore_fsck_error"].get("msg", ""))
                reply = vfy.command("", timeout=5.0)
                data = json.loads(reply)
                fsck = data.get("logstore_fsck", {})

            state = fsck.get("state", "")
            errors = fsck.get("errors", -1)

            if state == "fail" and errors == EXPECTED_ERRORS:
                results.passed("logstore_fsck_errors",
                    f"{errors} errors detected (expected {EXPECTED_ERRORS})")
            else:
                results.failed("logstore_fsck_errors",
                    f"state={state} errors={errors} (expected {EXPECTED_ERRORS})")
        except (RttError, json.JSONDecodeError) as e:
            results.failed("logstore_fsck_errors", str(e))

        # ----------------------------------------------------------------
        # logstore_cleanup — delete all test files
        # ----------------------------------------------------------------

        results.start("logstore_cleanup")
        try:
            for log_num, _, _, _, _ in TEST_CASES:
                reply = vfy.command(f"filesystem_test_delete log_t{log_num}.meta", timeout=5.0)
                data = json.loads(reply)
                state = data.get("filesystem_test_delete", {}).get("state", "")
                if state != "ok":
                    results.failed("logstore_cleanup", f"failed to delete log_t{log_num}.meta: {reply}")
                    return
            results.passed("logstore_cleanup", f"{len(TEST_CASES)} test files deleted")
        except (RttError, json.JSONDecodeError) as e:
            results.failed("logstore_cleanup", str(e))

        # ----------------------------------------------------------------
        # logstore_fsck_clean — FSCK should pass after cleanup
        # ----------------------------------------------------------------

        results.start("logstore_fsck_clean")
        try:
            reply = vfy.command("logstore_fsck", timeout=30.0)
            data = json.loads(reply)
            fsck = data.get("logstore_fsck", {})
            state = fsck.get("state", "")
            errors = fsck.get("errors", -1)
            if state == "pass" and errors == 0:
                results.passed("logstore_fsck_clean")
            else:
                results.failed("logstore_fsck_clean", f"state={state} errors={errors}")
        except (RttError, json.JSONDecodeError) as e:
            results.failed("logstore_fsck_clean", str(e))

        # ----------------------------------------------------------------
        # logstore_chunk_crossing — write/read across chunk boundary
        # ----------------------------------------------------------------

        results.start("logstore_chunk_crossing")
        try:
            reply = vfy.command("logstore_test_chunk_crossing", timeout=30.0)
            data = json.loads(reply)
            test = data.get("logstore_test_chunk_crossing", {})
            state = test.get("state", "")
            if state == "pass":
                results.passed("logstore_chunk_crossing")
            else:
                results.failed("logstore_chunk_crossing",
                    test.get("reason", f"state={state}"))
        except (RttError, json.JSONDecodeError) as e:
            results.failed("logstore_chunk_crossing", str(e))
