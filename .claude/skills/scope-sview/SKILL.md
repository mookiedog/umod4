---
name: scope-sview
description: Drive and read an HP/Agilent 16700-series logic analyzer scope (HP 16702A with a 16534A scope module) using the sview project, to capture and analyze real waveforms. Use when a scope measurement is needed, when verifying EPROM emulator timing, or when the user mentions sview, .scap files, the HP 16702A, the 16534A, RPI, or a scope capture.
---

# Using the scope through sview

The owner has a project called **sview** (`~/projects/sview`, GitHub `mookiedog/sview`).
It talks to an HP 16702A over the network and can capture, store and analyze scope data.
The owner's description: it gives an agent complete control over the scope.
This skill records what I verified by reading its README, CLI source and the vendor guide.
I have NOT run any of it against hardware in this session.

The owner has two scopes:

- **Agilent 54622D.** It does NOT work with sview. Nothing in this skill applies to it.
  I assume (not verified) that this is the scope where the infinite-persistence procedure is done by hand, and I do not know whether it can be driven remotely.
- **The scope module in the HP 16702A** (the sview README names it as a 16534A). This is the only scope that works with sview.

Ask which scope the user means before assuming this skill applies.

## What sview is

- It speaks RPI, the instrument's ASCII/binary protocol over TCP port 6500.
  RPI has **no authentication**: anyone who can reach the port can drive the instrument.
- Tested by its author against a 16702A with a 16534A 2-channel scope module.
- Run everything from `~/projects/sview` with `uv run`. Do not cd elsewhere and guess.
- The vendor programmer's guide is `~/projects/sview/doc/RPI-9018-40680.pdf`. `pdftotext` is installed, so it can be searched.
  It documents the scope commands, for example `-tgsource`, `-tglevel`, `-tgslope` and `-tgmode`.

## Commands

| Command | What it does |
|---------|--------------|
| `uv run sview-capture modules <host>` | List the modules in the frame. Harmless read |
| `uv run sview-capture capture <host> --out-dir <dir>` | Pull the scope's LAST acquisition into a `.scap` file. Add `--scope-name "Scope<C>"` only if several scope modules exist |
| `uv run sview-capture shell <host>` | Raw RPI command prompt, for anything the other commands do not cover |
| `uv run sview-capture show-measurements`, `export-slice`, `measure-overshoot` | Read an existing `.scap` file. No hardware needed |
| `uv run sview --captures-dir <dir>` | Local web viewer at http://127.0.0.1:8000 with a Capture Now button |
| `uv run sview-capture activate <host>` | **Loads sview's default scope configuration and REPLACES the instrument's workspace** |
| `uv run sview-capture deactivate <host> --yes` | **Clears the whole workspace** (testing aid) |

`capture` takes whatever the scope last acquired, so the scope must be armed and triggered first (from the front panel, or with start/wait over RPI).
`examples/` has analysis scripts: FFT, band-pass filter, damping, and threshold or derivative event finding.
They need numpy or scipy, which the core package does not carry.

## Rules

- **Ask before touching the instrument.**
  Get the scope's address from the user. The `192.168.1.173` in the sview README is the author's own bench setup, not a default.
- **Never run `activate` or `deactivate` without explicit permission.**
  Both change or erase the instrument's workspace.
- A direct Ethernet cable to the scope needs the PC's adapter put on the scope's subnet.
  `scripts/connect_scope_ethernet.sh` does that and needs sudo, so the user must run it.
  Say exactly what it does first.
- Save captures from an investigation into that investigation's own directory with `--out-dir`.
  sview's own `captures/` folder is gitignored scratch.
  Lab notes in this repo under `doc/__investigations/` stay untracked, so do not commit captures there.
- Report voltages and timings as the instrument's numbers.
  The README records a small known bias (about +0.18% gain and +2.9 mV offset on the channel that was DMM-tested), and says other channels and more than two channels are uncharacterized.

## Using it for EPROM timing verification

The owner's scope procedure for the emulator mainloop is in the ecu-ep-safety skill: infinite persistence, E and DBG_BSY, trigger on falling E, and check the last DBG_BSY down-pulse before the next falling E.

What is established and what is not:

- **Not established: infinite persistence over RPI.**
  The vendor RPI guide contains no mention of persistence, so I cannot claim that the display mode can be set remotely.
  It may be a front-panel setting on this instrument.
- **Not established: an equivalent method with sview.**
  Repeated single captures, then event finding across them, is a plausible substitute for a persistent display.
  It has not been tried, and whether the 16534A's sample rate and memory resolve the DBG_BSY pulse against E is unchecked.
- **Not established: how a human-judged "one clean pulse, no smear" maps to a numeric pass/fail.**

So: do not tell the user a timing change is verified on the strength of this skill.
Propose a concrete capture-and-analysis plan, say which parts are untried, and let the user decide whether to run it.
If the owner works out a reliable method, record it here.

## Keeping this skill current

At the end of a session that used this skill, if you learned something not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
