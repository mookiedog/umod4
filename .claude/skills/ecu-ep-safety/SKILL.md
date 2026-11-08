---
name: ecu-ep-safety
description: Safety and timing-verification rules for changing the EPROM emulator (epromEmulator.S and the HC11 bus PIO code). Use before and after editing epromEmulator.S, hc11Sync.pio, busFailsafe.pio, the EPROM mainloop, or ECU assembly (UM4.S, general rules only until an ECU skill exists), and whenever someone asks whether a firmware change is safe for the bike.
---

# Changing the dangerous areas

A bad ECU or EP change can stall or damage a running engine, or strand the rider.
AGENTS.md section 4 sets the general rule: never call a firmware change safe for the bike unless it was bench-tested, and always say what was and was not tested.
This skill adds the specific rules for the EPROM emulator.

## Rules for `EP/src/epromEmulator.S` and the PIO state machines (set by the owner)

1. **Any change other than documentation needs scope verification.**
2. **Any change to any part of the EPROM mainloop MUST have its timing verified.**
3. **Any change MUST update the timing calculations in the mainloop comments.**

The PIO state machines that touch the HC11 bus are part of the EPROM emulation timing, so these rules cover them too: `EP/src/hc11Sync.pio` and `EP/src/busFailsafe.pio`, and their setup code (`hc11SyncSetup.cpp`, `busFailsafeSetup.cpp`).
`uart_tx32.pio` is the EP-to-WP UART, not the HC11 bus. I assume it is not covered, but confirm with the user if a change there could affect bus timing.

## Why the timing is tight

All of this is from the comments at the top of `epromEmulator.S`; read them before editing.

- The HC11 E clock is 2 MHz, and the emulator runs at 125 MHz, so one RP2040 clock is 8 nS.
- That leaves about 60 clocks per E cycle. The loop targets finishing within 58 clocks in the worst case, so the CPU can get back to `wfi`.
- Every instruction is annotated `x [y]`: `x` is the cycles it takes and `y` is the cumulative cycles since falling E.
- A taken branch costs 1 extra clock. The target label of every branch must sit on an otherwise empty source line, and the label is billed for that clock.
- Every path (read, write, ignore) must reach the shared `log:` label at exactly the same cycle count. The comments state the value, and the code carries the reasoning.
- The existing comments record a measured margin at the `wfi` (about 3 clocks, 24 nS, at the time of writing).
  A change can eat it.

## Procedure for a change to the mainloop

1. Read the timing notes at the top of the file and the path you are changing.
2. Make the change, then redo the accounting: recompute every `[y]` cumulative count that moved, on every path that is affected.
3. Check that all paths still arrive at `log:` at the same count, and that the worst case still fits the budget.
4. Update the comments: the per-instruction `x [y]` values, and any measured-margin statements that the change makes untrue.
5. You cannot use a scope. Hand the verification procedure below to the user, and treat the change as UNVERIFIED until they report the result.
6. When reporting, say plainly what was checked by arithmetic and what was measured on hardware. Do not call the change safe for the bike.

## Scope procedure (the owner's method)

1. Put the scope in infinite persistence mode.
2. Probe E and DBG_BSY.
3. Trigger on falling E.
4. Look at the final down-pulse of DBG_BSY before the next falling E.
5. It must be consistent across time, meaning across reads, writes and ignored cycles. A persistent display should show one clean pulse, not a smear or outliers.

The scope can be driven by the agent in some setups: see the scope-sview skill.
That skill describes what is and is not established about doing this procedure remotely.

The pulse comes from the two DBG_BSY instructions in the `#if 1` block at the end of the `log:` path.
Keep that block enabled while verifying.
If the change moves the pulse, update the margin comment in that block with the new measurement.

## Other dangerous code

- **Other EP code** (`epromEmulator.h`, core 1 code): the rules were stated for `epromEmulator.S` and the PIO machines. If a change could affect bus timing, treat it as needing scope verification, and ask the user if unsure.
- **PIO changes:** the scope procedure above was described for the mainloop.
  Which signals to probe for a change to a PIO program is not recorded. Ask the user.
- **ECU assembly (`ecu/src/UM4.S`):** the owner wants this to have its own skill, which does not exist yet.
  Until then, follow AGENTS.md section 4: explain the risk, require bench testing (see BENCH_TESTING.md), and state what was not tested.
  Ask the user what verification they want for the change at hand.
- **Power circuitry:** hardware, not firmware. Do not guess. Point to the hardware documentation.

## Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
