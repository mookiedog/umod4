"""
ECU status tests — verifies ECU firmware is alive and logging correctly.

Uses the E-clock frequency reported by the EP to detect whether the ECU is
powered before committing to the observation window.  If eclk_khz is near
zero the ECU is unpowered and all ECU checks are skipped.

After test_ota_ep reflashes the EP, the ECU reboots and emits a RESET log
event during its initialization.  This suite waits for that RESET event to
appear before taking the baseline snapshot (ecu0), so the 30-second
observation window captures only post-boot activity.  All sensor event counts
are measured as deltas (ecu1 - ecu0) to avoid contamination from events that
accumulated before the EP reflash.
"""

import json
import time

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL   = 1
ECU_SETTLE_SECS  = 30    # observation window length
ECU_RESET_VAL    = 7     # LOGID_ECU_CPU_EVENT_RESET_VAL
ECLK_MIN_KHZ     = 1800  # below this → ECU not powered (2 MHz E-clock → ~2000 kHz)
ECU_BOOT_TIMEOUT  = 15   # seconds to wait for ECU reset sequence to complete
ECU_BOOT_STABLE   = 3    # cpu_events must be unchanged for this many seconds

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


def _get_ecu(vfy):
    """Send 'ecu' and return the inner dict, or raise RttError / ValueError."""
    reply = vfy.command("ecu", timeout=5.0)
    data  = json.loads(reply)
    inner = data.get("ecu")
    if inner is None:
        raise ValueError(f"no 'ecu' key in reply: {reply}")
    return inner


def _delta(ecu0, ecu1, key):
    """Increase in an accumulated counter between two snapshots.
    Clamps at 0 to handle a counter reset mid-window (e.g. unexpected reboot)."""
    return max(0, ecu1.get(key, 0) - ecu0.get(key, 0))


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # Phase 1a: verify ECU is powered via E-clock frequency
        try:
            snap = _get_ecu(vfy)
        except (RttError, ValueError, json.JSONDecodeError) as e:
            results.abort("ecu", str(e))
            return

        eclk_khz = snap.get("eclk_khz", 0)
        if eclk_khz < ECLK_MIN_KHZ:
            results.abort("ecu_not_powered",
                          f"eclk_khz={eclk_khz} — ECU unpowered")
            return

        results.passed("ecu_powered", f"eclk_khz={eclk_khz}")

        # ----------------------------------------------------------------
        # Phase 1b: wait for the ECU reset sequence to complete.
        # After test_ota_ep reflashes the EP, the ECU may reset more than
        # once: EP Core 1 starts EPROM emulation (ECU gets bus → reset #1),
        # then Core 0 finishes loading the full EPROM image from SPI flash
        # and re-asserts reset (reset #2).  Poll until cpu_events has been
        # stable for ECU_BOOT_STABLE seconds before taking the baseline so
        # that all initialization resets are captured in ecu0, not the delta.
        print(f"  Waiting up to {ECU_BOOT_TIMEOUT}s for ECU reset sequence to settle...",
              flush=True)
        ecu0      = None
        deadline  = time.monotonic() + ECU_BOOT_TIMEOUT
        prev_cpu  = -1
        stable_at = None
        while ecu0 is None:
            try:
                snap = _get_ecu(vfy)
                cpu  = snap.get("cpu_events", 0)
                if cpu != prev_cpu:
                    prev_cpu  = cpu
                    stable_at = time.monotonic() if cpu > 0 else None
                elif stable_at is not None:
                    if time.monotonic() - stable_at >= ECU_BOOT_STABLE:
                        ecu0 = snap
            except (RttError, ValueError, json.JSONDecodeError):
                stable_at = None   # treat a comm error as instability
            if ecu0 is None:
                if time.monotonic() >= deadline:
                    results.abort("ecu_boot",
                                  f"ECU did not stabilize within {ECU_BOOT_TIMEOUT}s "
                                  f"(last cpu_events={prev_cpu})")
                    return
                time.sleep(0.5)

        cpu_last_boot = ecu0.get("cpu_last", -1)
        boot_resets   = ecu0.get("cpu_events", 0)
        if cpu_last_boot == ECU_RESET_VAL:
            results.passed("ecu_boot",
                           f"stable after {boot_resets} reset(s), last val={cpu_last_boot}")
        else:
            results.failed("ecu_boot",
                           f"cpu_last={cpu_last_boot}, expected {ECU_RESET_VAL} (RESET), "
                           f"resets={boot_resets}")

        # ----------------------------------------------------------------
        # Phase 2: 30-second observation window, then end snapshot.
        print(f"  Waiting {ECU_SETTLE_SECS}s observation window...", flush=True)
        time.sleep(ECU_SETTLE_SECS)

        try:
            ecu1 = _get_ecu(vfy)
        except (RttError, ValueError, json.JSONDecodeError) as e:
            results.abort("ecu_final", str(e))
            return

        # ----------------------------------------------------------------
        # ECU metadata — confirms ECU firmware identity
        meta = ecu1.get("meta", "")
        if meta:
            results.passed("ecu_metadata", json.dumps(meta) if isinstance(meta, dict) else meta)
        else:
            results.failed("ecu_metadata", "empty — ECU metadata not received")

        # ----------------------------------------------------------------
        # CPU events — expect no resets during the observation window.
        # The boot reset was already captured in ecu0, so delta=0 means
        # the ECU ran stably for the full window with no crashes.
        delta_cpu = _delta(ecu0, ecu1, "cpu_events")
        if delta_cpu == 0:
            results.passed("cpu_events", "no resets during observation window")
        else:
            results.failed("cpu_events",
                           f"{delta_cpu} reset(s) during window, last val={ecu1.get('cpu_last', -1)}")

        # ----------------------------------------------------------------
        # T1 period check:
        # Check the inter-event period (time between two consecutive T1_OFLO
        # arrivals as timestamped by WP) rather than counting events over a
        # window.  This is immune to window-timing bias in the test harness.
        t1_period = ecu1.get("t1_period_us", 0)
        t1_lo     = int(T1_PERIOD_US * (1.0 - T1_TOLERANCE))
        t1_hi     = int(T1_PERIOD_US * (1.0 + T1_TOLERANCE))
        if t1_period == 0:
            results.failed("t1_period",
                           "t1_period_us=0 — ECU main loop not running or fewer than 2 events received")
        elif t1_lo <= t1_period <= t1_hi:
            results.passed("t1_period",
                           f"t1_period_us={t1_period} (expected ~{T1_PERIOD_US})")
        else:
            results.failed("t1_period",
                           f"t1_period_us={t1_period}, expected {t1_lo}-{t1_hi}")

        # ----------------------------------------------------------------
        # Sensor ratio check.
        # All ratios are relative to VM (64/256 ADC cycles).  From UM4.S:
        #   VTA  128/256 →  2.0 × VM   (optional — may be absent per L000C)
        #   VM    64/256 →  1.0 × VM   (required)
        #   THA   32/256 →  0.5 × VM   (optional — may be absent per L000C)
        #   AAP   16/256 →  0.25 × VM  (required)
        #   THW    2/256 → 1/32 × VM   (optional — may be absent per L000C)
        #   TP1    1/256 → 1/64 × VM   (required)
        #   TP2    1/256 → 1/64 × VM   (required)
        #
        # Optional sensors with delta=0 must be flagged bad in L000C:
        #   bit 3 (0x08): VTA bad   bit 2 (0x04): THW bad   bit 1 (0x02): THA bad
        RATIO_TOL = 0.02   # ±2%

        vta_count = _delta(ecu0, ecu1, "vta_count")
        vm_count  = _delta(ecu0, ecu1, "vm_count")
        tha_count = _delta(ecu0, ecu1, "tha_count")
        aap_count = _delta(ecu0, ecu1, "aap_count")
        thw_count = _delta(ecu0, ecu1, "thw_count")
        tp1_count = _delta(ecu0, ecu1, "tp1_count")
        tp2_count = _delta(ecu0, ecu1, "tp2_count")
        l000c     = ecu1.get("l000c_last", 0)

        if vm_count == 0:
            results.failed("sensor_ratios",
                           f"VM=0 — required sensor has no events; l000c=0x{l000c:02x}")
        else:
            # (name, count, expected_ratio, required, l000c_bit_or_None)
            checks = [
                ("VTA", vta_count, 2.0,    False, 0x08),
                ("THA", tha_count, 0.5,    False, 0x02),
                ("AAP", aap_count, 0.25,   True,  None),
                ("THW", thw_count, 1/32,   False, 0x04),
                ("TP1", tp1_count, 1/64,   True,  None),
                ("TP2", tp2_count, 1/64,   True,  None),
            ]
            failures  = []
            summaries = [f"VM={vm_count}"]
            for name, count, expected, required, l000c_bit in checks:
                if count == 0:
                    if required:
                        failures.append(f"{name}=0 (required)")
                    elif l000c_bit and (l000c & l000c_bit):
                        summaries.append(f"{name}=absent(L000C)")
                    else:
                        failures.append(
                            f"{name}=0 but L000C bit 0x{l000c_bit:02x} not set")
                else:
                    ratio = count / vm_count
                    lo    = expected * (1.0 - RATIO_TOL)
                    hi    = expected * (1.0 + RATIO_TOL)
                    summaries.append(f"{name}={count}({ratio:.4f}x)")
                    if not (lo <= ratio <= hi):
                        failures.append(
                            f"{name}/VM={ratio:.4f} expected {expected:.4f} "
                            f"±{RATIO_TOL*100:.0f}%")
            summary = "  ".join(summaries) + f"  l000c=0x{l000c:02x}"
            if failures:
                results.failed("sensor_ratios",
                               f"{'; '.join(failures)}  {summary}")
            else:
                results.passed("sensor_ratios", summary)

        # ----------------------------------------------------------------
        # l000c error register: assert expected value for the bench sensor setup.
        # Use --l000c on the command line to override if your bench differs.
        expected_l000c = context.get("l000c")
        if expected_l000c is None:
            expected_l000c = EXPECTED_L000C
        l000c_count = ecu1.get("l000c_count", 0)
        l000c_last  = ecu1.get("l000c_last",  0)
        if l000c_last == expected_l000c:
            results.passed("error_l000c",
                           f"l000c=0x{l000c_last:02x} (expected), count={l000c_count}")
        else:
            results.failed("error_l000c",
                           f"l000c=0x{l000c_last:02x}, expected 0x{expected_l000c:02x}, count={l000c_count}")
