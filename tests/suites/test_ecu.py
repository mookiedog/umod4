"""
ECU status tests — verifies ECU firmware is alive and logging correctly.

Uses the E-clock frequency reported by the EP to detect whether the ECU is
powered before committing to the 30-second accumulation wait.  If eclk_khz
is near zero the ECU is unpowered and all ECU checks are skipped.

Assertions that are currently informational (aap_count, vm_count,
error_l000c) always pass and report their values; once real-hardware
baselines are established those checks can be tightened.
"""

import json
import time

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL   = 1
ECU_SETTLE_SECS  = 30    # let ECU accumulate events before sampling counts
ECU_RESET_VAL    = 7     # LOGID_ECU_CPU_EVENT_RESET_VAL
ECLK_MIN_KHZ     = 1800  # below this → ECU not powered (2 MHz E-clock → ~2000 kHz)

# Expected L000C error register value for the reference bench setup:
#   present:  VTA (throttle angle), AAP (ambient air pressure)
#   absent:   air temp, water temp, MAP
# Override with --l000c on the command line if your bench differs.
EXPECTED_L000C   = 0x16

# T1 overflow period: HC11 free-running timer, prescaler 4, 2 MHz E-clock.
# 65536 counts × 4 prescaler / 2,000,000 Hz = 131,072 µs per overflow.
# Log analysis (635s, 4851 events) confirms hardware runs within 0.13% of
# theoretical.  WP timestamps arrival of each T1_OFLO event; the difference
# between two consecutive arrivals is the inter-event period — immune to any
# window-timing bias in the test harness.
T1_PERIOD_US     = 65536 * 4 * 1_000_000 // 2_000_000   # 131072 µs
T1_TOLERANCE     = 0.005                                 # ±0.5%
VM_AAP_MIN_RATIO = 2                                     # VM must be at least 2× AAP


def _get_ecu(vfy):
    """Send 'ecu' and return the inner dict, or raise RttError / ValueError."""
    reply = vfy.command("ecu", timeout=5.0)
    data  = json.loads(reply)
    inner = data.get("ecu")
    if inner is None:
        raise ValueError(f"no 'ecu' key in reply: {reply}")
    return inner


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # Phase 1: detect ECU power via E-clock frequency (no wait needed)
        try:
            ecu = _get_ecu(vfy)
        except (RttError, ValueError, json.JSONDecodeError) as e:
            results.abort("ecu", str(e))
            return

        eclk_khz  = ecu.get("eclk_khz", 0)

        if eclk_khz < ECLK_MIN_KHZ:
            results.abort("ecu_not_powered",
                          f"eclk_khz={eclk_khz} — ECU unpowered")
            return

        results.passed("ecu_powered", f"eclk_khz={eclk_khz}")

        # ----------------------------------------------------------------
        # Phase 2: wait for ECU to accumulate events, then check
        print(f"  Waiting {ECU_SETTLE_SECS}s for ECU to accumulate log events...", flush=True)
        time.sleep(ECU_SETTLE_SECS)

        try:
            ecu = _get_ecu(vfy)
        except (RttError, ValueError, json.JSONDecodeError) as e:
            results.abort("ecu_final", str(e))
            return

        # ----------------------------------------------------------------
        # ECU metadata — confirms ECU firmware identity
        meta = ecu.get("meta", "")
        if meta:
            results.passed("ecu_metadata", json.dumps(meta) if isinstance(meta, dict) else meta)
        else:
            results.failed("ecu_metadata", "empty — ECU metadata not received")

        # ----------------------------------------------------------------
        # CPU events — expect exactly 1 RESET event, nothing else
        cpu_events = ecu.get("cpu_events", 0)
        cpu_last   = ecu.get("cpu_last", -1)
        if cpu_events == 1 and cpu_last == ECU_RESET_VAL:
            results.passed("cpu_events", f"count=1, val={cpu_last} (RESET)")
        elif cpu_events == 0:
            results.failed("cpu_events", "no CPU events — ECU may not be executing")
        elif cpu_events == 1 and cpu_last != ECU_RESET_VAL:
            results.failed("cpu_events", f"count=1 but val={cpu_last}, expected {ECU_RESET_VAL} (RESET)")
        else:
            results.failed("cpu_events", f"count={cpu_events}, last={cpu_last} — expected exactly 1 RESET")

        # ----------------------------------------------------------------
        # T1 period check:
        # Check the inter-event period (time between two consecutive T1_OFLO
        # arrivals as timestamped by WP) rather than counting events over a
        # window.  This is immune to window-timing bias in the test harness.
        t1_period = ecu.get("t1_period_us", 0)
        t1_lo     = int(T1_PERIOD_US * (1.0 - T1_TOLERANCE))
        t1_hi     = int(T1_PERIOD_US * (1.0 + T1_TOLERANCE))
        if t1_period == 0:
            results.failed("t1_period", "t1_period_us=0 — ECU main loop not running or fewer than 2 events received")
        elif t1_lo <= t1_period <= t1_hi:
            results.passed("t1_period",
                           f"t1_period_us={t1_period} (expected ~{T1_PERIOD_US})")
        else:
            results.failed("t1_period",
                           f"t1_period_us={t1_period}, expected {t1_lo}-{t1_hi}")

        # ----------------------------------------------------------------
        # AAP and VM: VM fires ~4× per AAP cycle (±VM_AAP_TOLERANCE).
        # The ECU only logs sensor readings when they change, so on a static
        # bench the ratio may not be exact, but should be in the right ballpark.
        aap_count = ecu.get("aap_count", 0)
        vm_count  = ecu.get("vm_count",  0)
        results.passed("aap_count", f"count={aap_count}, last={ecu.get('aap_last',0)}")
        if aap_count == 0:
            results.failed("vm_count",
                           f"aap_count=0 — cannot check VM:AAP ratio (count={vm_count})")
        else:
            vm_min = aap_count * VM_AAP_MIN_RATIO
            if vm_count >= vm_min:
                results.passed("vm_count",
                               f"count={vm_count}, ratio={vm_count/aap_count:.2f}× AAP (min {VM_AAP_MIN_RATIO}×)")
            else:
                results.failed("vm_count",
                               f"count={vm_count}, ratio={vm_count/aap_count:.2f}× AAP, expected >={vm_min} (min {VM_AAP_MIN_RATIO}×)")

        # ----------------------------------------------------------------
        # l000c error register: assert expected value for the bench sensor setup.
        # Use --l000c on the command line to override if your bench differs.
        expected_l000c = context.get("l000c")
        if expected_l000c is None:
            expected_l000c = EXPECTED_L000C
        l000c_count = ecu.get("l000c_count", 0)
        l000c_last  = ecu.get("l000c_last",  0)
        if l000c_last == expected_l000c:
            results.passed("error_l000c",
                           f"l000c=0x{l000c_last:02x} (expected), count={l000c_count}")
        else:
            results.failed("error_l000c",
                           f"l000c=0x{l000c_last:02x}, expected 0x{expected_l000c:02x}, count={l000c_count}")
