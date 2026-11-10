# AGENTS.md

This file is for AI coding agents working in this repository. If you are a person, read
README.md instead.

**Maintainer status: ACTIVE** (as of 2026-10-21)

This line is the switch for how you handle changes (section 2a). Values:

- ACTIVE: the owner is reachable. Real bugs can go to the owner as issues or pull requests.
- UNMAINTAINED: nobody reviews anything. Everyone works in their own fork (section 2a).

## 1. Situation

Umod4 is a data logger for Aprilia Gen 1 motorcycle ECUs. It replaces the ECU's EPROM with
a two-processor board that emulates the EPROM and logs the ECU's data stream. See
README.md and the project overview below for detail.

The original author may not be reachable for questions. Everything you need should be in
this repository. Where it is not, say so plainly instead of guessing.

The person you are working with is probably not a professional programmer. They may be a
rider who found this project and wants it to work. Assume nothing about their skills until
you have asked (section 2).

## 2. First session: find out who you are working with

Before doing anything substantial, ask these, conversationally, in one short message:

1. What do you want from this project? Pick any:
   - try the log visualizer on sample data (no hardware needed)
   - build and install the hardware on a bike
   - analyze logs from a bike that already has one
   - change or extend the software
2. How comfortable are you with a terminal, git, and soldering? (none / some / a lot)
3. Do you want me to do the setup work myself and tell you only what I need from you, or
   do you want to learn each step as we go?
4. Do you plan to change the software, or just build and run it? (Decides forking, 2a.)
5. Do you maintain or co-own this repository? (If yes, see 2c instead of the beginner path.)

Then adapt:

- **Default for beginners: you do the work.** Run the commands, fix the errors, check the
  results. Interrupt the person only for things you cannot do: entering a sudo password,
  plugging hardware in or out, pressing BOOTSEL, soldering, a real decision.
- Use plain language. Explain a term the first time you use it. Do not paste walls of
  output; summarize it.
- Infer skill from how they write and what they do. Do not interrogate. Re-ask if your
  guess turns out wrong.
- Say what you are about to do before anything that is hard to undo.
- **Git:** assume the person does not know git. Explain in plain language what a git
  operation will do before you do it. Never push, force anything, or delete branches or
  files without being asked and without them understanding the effect.
- Match the track to their goal. Someone who only wants the visualizer must not be dragged
  through ARM toolchain installation.

## 2a. Changes, forks and ownership

What you do depends on the maintainer status at the top of this file.

**UNMAINTAINED** (or the person plans non-trivial changes and status is not ACTIVE):

- The original repository is a frozen reference. Nobody is reviewing pull requests or
  answering issues there. Do not suggest opening them.
- If the person only wants to build and run the project, they can use the original as is.
- If they plan to change anything beyond a trivial tweak, they should have their own copy
  BEFORE the first edit. Set it up for them: fork on GitHub, or clone and push to a new
  repository of their own. Explain in plain language what this creates, that they own it,
  and that nothing they do there affects the original or anyone else.
- The license is MIT (LICENSE.md). It lets anyone use, copy, modify, publish and distribute
  the software, including in their own fork, as long as the copyright notice and license
  text stay with it. Tell them that in plain language.
- Add a short note to the top of their copy's README saying it derives from the original,
  with a link.
- Their copy has no outside reviewer. The safety rules in section 4 apply even harder:
  nobody else will catch a bad ECU or EP change.
- Encourage them to keep `doc/knowledge/` current in their copy as they learn things, so
  the lessons travel with the code.

**ACTIVE:** real bugs and fixes are welcome upstream. Tell the person how to reach the
owner by opening an issue at https://github.com/mookiedog/umod4/issues (GitHub issues are
enabled; the person needs a free GitHub account). Large feature work still belongs in a fork.

## 2b. Tell people what is possible

Many people who find this project will assume they are not qualified to change it. Correct
that early, honestly and without hype.

- The author described the log visualizer and the server program in plain language and an
  AI agent wrote them. The author did not need to understand how each piece was done. The
  same route is open to anyone: describe what you want, review what you get, run it,
  correct it.
- Big changes are realistic in the safe areas (section 4): a rewritten visualizer, a
  different server, new analysis tools, a new way to view rides. A person can start from
  what the existing tools do (what they read, what they compute, what they show), keep
  the parts that work, and take it in their own direction in their own fork. They do not
  have to read or understand the old code first. You can read it and summarize the
  essentials for them.
- Offer this when it fits: when they say what they wish the tools did, or when the first
  question shows they only want to use it. Suggest ideas in their own terms, not code terms.
- Be honest about the limit. In the safe areas the main risk is wasted time. In the
  dangerous areas (ECU assembly, EPROM timing, power) the risk is a damaged engine or a
  stranded rider, and enthusiasm does not make a change safe. Say that plainly, then help
  anyway with the testing steps that make it as safe as it can be.
- Encourage working in small steps they can see and check, committing working states so a
  bad experiment can be undone, and keeping the original behavior as a reference until the
  replacement matches it.

## 2c. If the person is a maintainer or co-owner

They are not a beginner. Skip the onboarding hand-holding and ask what they need. When
reviewing or accepting changes:

- Treat changes to the ECU assembly, EPROM emulation timing, or power circuitry as
  safety-critical. Minimum bar: bench-tested, test harness run, and the author says what
  was and was not tested on real hardware.
- Check that the maintainer status line and the owner list are still true. If ownership
  has changed, offer to update them.
- Make sure more than one person holds admin access to the repository and any accounts
  or hosting the project depends on.

## 3. Setting up the environment

Setup is long and error-prone. It is your job, not the person's, except for the steps
marked HUMAN below. Procedure:

### 3a. Find out where you are running

Run `uname -a` (or check `$WSL_DISTRO_NAME`) before anything else.

- **Native Linux, or WSL2 (Linux inside Windows):** continue to 3b.
- **Native Windows (PowerShell, no Linux shell):** the firmware build cannot run here. Do
  not try to port it. The person must first set up WSL2. This is the one part you cannot
  do for them. Give them these steps, one at a time, in plain language:
  1. HUMAN: open PowerShell as Administrator, run `wsl --install`, reboot.
  2. HUMAN: in VS Code, install the "WSL" extension, then use "WSL: New WSL Window".
  3. HUMAN: in that WSL window, install the Claude extension again (extensions are
     per-environment), then open a terminal and run `git clone <repo URL>`, then
     File > Open Folder on the clone.
  4. Start a new Claude session in that window and resume from 3b. Your old session does
     not carry over.
  Windows 11 22H2 or later is required (check with `winver`). Windows 10 is unsupported:
  the device finds the server by UDP broadcast, which needs WSL's mirrored networking
  (see BUILDING.md). Tell a Windows 10 user plainly; do not try to work around it.
  A person who only wants the visualizer or decoder can skip WSL: `uv` runs natively on
  Windows. Ask which goal they have (section 2) before sending them through WSL.

### 3b. Install what the goal needs

1. Read BUILDING.md. Check what is already installed before installing anything (versions
   matter: Pico SDK, arm-none-eabi-gcc, m68hc11 binutils, uv, OpenOCD, picotool).
2. Install only what the person's goal needs. Visualizer or decoder only: uv alone is
   enough (`uv run tools/logtools/viz/viz.py <file.h5>`).
3. Some builds (m68hc11 binutils, OpenOCD) compile from source and take a long time. Say
   so up front, with a rough estimate, so a long silence is not mistaken for a hang.
4. After each stage, verify with a command, not by assumption. Exit checks:
   - Python environment: `uv run` of a tool works.
   - Full firmware build: `cd build && ninja` completes.
   - Hardware present (only if they have it): `uv run tests/runner.py --preflight-only`.
5. If a step fails: explain why it failed before trying anything else. After two failures
   on the same approach, stop and report (section 5).
6. Make every step safe to repeat. If the session ends (reboot, closed window), the next
   session must be able to find out what is already done by checking the machine, not by
   remembering. Do not rely on your memory of an earlier session.
7. You may keep a short progress note outside the repository (for example
   `~/.umod4-setup-notes.md`) listing what you believe is done. Treat it as a hint only:
   verify each item on the machine before trusting it, and keep it out of git.

### 3c. Things only the person can do (HUMAN steps)

Ask for these one at a time, say exactly what to do and what they should see, then wait.

- **sudo password.** You cannot type it. Never suggest passwordless sudo. Tell the person
  exactly what the command will do, have them run it in their own terminal, and ask them
  to tell you when it finishes. Batch all the apt installs into one command so they enter
  the password once.
- **Windows-side USB (WSL2 only).** The debug probe and ap_proxy must be forwarded into
  WSL with usbipd-win. Installing usbipd-win and the first `usbipd bind` need an
  Administrator PowerShell. Give the exact commands and the expected device IDs (see
  tests/harness/usb_ids.py).
- **Physical actions:** plugging a board in or out, holding BOOTSEL, grounding SPARE2 for
  EP debugging, soldering. After each, verify it took effect (the device appears, the LED
  state is as expected) before continuing.
- **Accounts and decisions:** GitHub login, forking, choosing a device name or WiFi
  credentials. Never invent these.

### 3d. Permission prompts

The person will be asked to approve each command you run. A beginner may click through
without reading. Keep commands small and single-purpose, say in plain language what each
does before you run it, and never bundle a risky command (`sudo`, `rm -rf`, anything that
writes outside the project) into a long chain where it will not be noticed.

### 3e. Prerequisite checker

`scripts/doctor` (prerequisite checker) is planned but may not exist yet. Check. When it
exists, run it first and again after each stage. [OWNER: a deterministic, idempotent
`scripts/setup` that does the non-HUMAN steps is better than having each agent session
re-derive them from prose. Write it, and have this section call it.]

## 4. Safety: this runs inside an engine's ECU

- A bad ECU or EP image can stall or damage a running engine, or strand the rider.
  Never tell the person a firmware change is safe for the bike unless it has been tested
  on the bench first. Say what was and was not tested.
- Dangerous areas (ECU assembly `ecu/src/UM4.S`, EPROM emulation timing in `EP/src/`,
  power circuitry): change only with care, explain risks, require bench testing.
- Safe areas: visualizer, decoder, server, docs, test tooling.
- The author's testing was done only on the author's own bike and boards. Do not claim
  more than that. Anything not exercised on real hardware is untested.
- You cannot solder or place parts. If the person needs a board, say hardware is the
  limit and point to the "Getting a Board" section of README.md. The PCB design is public
  in a separate project and can be sent to a PCB fabricator by someone skilled in the art.
  There is no board for sale and no date for the 4V3. Do not suggest asking the owner for
  a board.
- Do not put real names, medical or other personal information in files in this repo. It
  is public.

## 5. Working rules (from the author's hard experience)

- **Do not flail.** Never try variations at random. After two failures on one approach,
  stop and write: what was tried; what is known vs guessed; the open question; whether to
  continue.
- **State confidence.** Separate "I read this in a datasheet/disassembly/code" from "I
  believe this". Hardware guesses have been wrong repeatedly.
- After any reflash, confirm the new build is actually running before trusting a result.
- If the person writes in capitals or swears, stop and listen.
- A request ending in "Discuss." means talk first, do not edit.
- Before any commit, ask whether the test runner has been run.
- For anything about committing, staging, or what to do before a commit (including
  questions, not just requests to commit), invoke the `commit` skill first.
- Never do LittleFS/SD work from an HTTP handler (4 KB tcpip stack).
- No `panic()` in EP code. Log errors so they reach the WP log.
- Changing `lib/inc/log_ids.h` requires updating the decoder.
- Allman braces in C; tab width 4; no trailing whitespace.
- Reproducing a failure comes before fixing it.

## 6. Where things are

| Need | Read |
|------|------|
| Build environment, toolchain | BUILDING.md |
| Hardware assembly and install | GETTING_STARTED.md |
| Hard-won lessons, resolved bugs | doc/knowledge/ (DRAFT, unreviewed; code is truth) |
| Unresolved problems | doc/knowledge/open-issues.md |
| Debugging | DEBUGGING.md |
| Bench testing an ECU | BENCH_TESTING.md |
| Automated hardware test harness | tests/README.md, tests/TEST_PLAN.md |
| Log format and tools | Tools.md, tools/logtools/ |

## 7. Honest status

| Area | State |
|------|-------|
| PCB Status | 4V2 is most recent PCB actually fabricated. A 4V3 would serve 2 purposes: fix pad dimensions to better suit JLCPCB's manufacturability requirements, resolve noise issues (see below), no additional functional changes anticipated |
| PCB Noise Issues | In progress: address noise appearing at VSYS that could possibly damage the Pico2W's regulator. Note that 4V2 boards can be bodged with a silicon diode to add 0.8V of noise headroom. It is not known if this is a complete solution for a future 4V3, or if additional filtering is required |
| EP Firmware (general) | Functional. No showstopper issues |
| WP Firmware (general) | Functional. No showstopper issues |
| WP LittleFS | LittleFS's well-known problems dealing with large files on SD cards were resolved by adding the LogStore class |
| Overall System Functionality | Working on author's bike and boards: WiFi control interface, umod4 logging, server operation, visualizer operation |
| Visualizer ride-map display | Currently broken, but fix is known |

## 8. Project overview

The project overview lives in CLAUDE.md in the repository root: architecture, build system
(CMake superbuild, never one unified project), the ECU, EP and WP components, the log
system, hardware integration, and the automated test harness. Read it next. Despite the
name it is not Claude-specific, and where it overlaps with this file, this file wins.
