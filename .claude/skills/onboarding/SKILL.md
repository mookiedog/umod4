---
name: onboarding
description: Onboard a newcomer to the umod4 project and set up their environment. Use when someone says they just found the project, wants to get started, asks what they can do with it, asks about forking or contributing, or wants to install the toolchain or set up the build on a new machine. Not for the maintainer's routine work.
---

# Onboarding a new person

This skill holds the first-session and setup procedure for people new to umod4.
AGENTS.md is the source for the maintainer-status line (ACTIVE or UNMAINTAINED), the safety rules (its section 4) and the working rules (its section 5).
Read those before acting on anything below.

## 1. Find out who you are working with

Before doing anything substantial, ask these, conversationally, in one short message:

1. What do you want from this project? Pick any:
   - try the log visualizer on sample data (no hardware needed)
   - build and install the hardware on a bike
   - analyze logs from a bike that already has one
   - change or extend the software
2. How comfortable are you with a terminal, git, and soldering? (none / some / a lot)
3. Do you want me to do the setup work myself and tell you only what I need from you, or do you want to learn each step as we go?
4. Do you plan to change the software, or just build and run it? (This decides forking, section 2.)
5. Do you maintain or co-own this repository? (If yes, use section 4 instead of the beginner path.)

Then adapt:

- **Default for beginners: you do the work.**
  Run the commands, fix the errors, check the results.
  Interrupt the person only for things you cannot do: entering a sudo password, plugging hardware in or out, pressing BOOTSEL, soldering, a real decision.
- Use plain language. Explain a term the first time you use it. Summarize output; do not paste walls of it.
- Infer skill from how they write and what they do. Do not interrogate. Re-ask if your guess turns out wrong.
- Say what you are about to do before anything that is hard to undo.
- **Git:** assume the person does not know git.
  Explain in plain language what a git operation will do before you do it.
  Never push, force anything, or delete branches or files without being asked and without them understanding the effect.
- Match the track to their goal. Someone who only wants the visualizer must not be dragged through ARM toolchain installation.

## 2. Changes, forks and ownership

What you do depends on the maintainer status line at the top of AGENTS.md.

**UNMAINTAINED** (or the person plans non-trivial changes and status is not ACTIVE):

- The original repository is a frozen reference. Nobody is reviewing pull requests or answering issues there. Do not suggest opening them.
- If the person only wants to build and run the project, they can use the original as is.
- If they plan to change anything beyond a trivial tweak, they should have their own copy BEFORE the first edit.
  Set it up for them: fork on GitHub, or clone and push to a new repository of their own.
  Explain in plain language what this creates, that they own it, and that nothing they do there affects the original or anyone else.
- The license is MIT (LICENSE.md).
  It lets anyone use, copy, modify, publish and distribute the software, including in their own fork, as long as the copyright notice and license text stay with it.
  Tell them that in plain language.
- Add a short note to the top of their copy's README saying it derives from the original, with a link.
- Their copy has no outside reviewer. The safety rules in AGENTS.md section 4 apply even harder: nobody else will catch a bad ECU or EP change.

**ACTIVE:** real bugs and fixes are welcome upstream.
Tell the person how to reach the owner by opening an issue at https://github.com/mookiedog/umod4/issues (the person needs a free GitHub account).
Large feature work still belongs in a fork.

## 3. Tell people what is possible

Many people who find this project will assume they are not qualified to change it.
Correct that early, honestly and without hype.

- The author described the log visualizer and the server program in plain language and an AI agent wrote them.
  The author did not need to understand how each piece was done.
  The same route is open to anyone: describe what you want, review what you get, run it, correct it.
- Big changes are realistic in the safe areas (AGENTS.md section 4): a rewritten visualizer, a different server, new analysis tools, a new way to view rides.
  A person can start from what the existing tools do (what they read, what they compute, what they show), keep the parts that work, and take it in their own direction in their own fork.
  They do not have to read or understand the old code first. You can read it and summarize the essentials for them.
- Offer this when it fits: when they say what they wish the tools did, or when the first question shows they only want to use it.
  Suggest ideas in their own terms, not code terms.
- Be honest about the limit.
  In the safe areas the main risk is wasted time.
  In the dangerous areas (ECU assembly, EPROM timing, power) the risk is a damaged engine or a stranded rider, and enthusiasm does not make a change safe.
  Say that plainly, then help anyway with the testing steps that make it as safe as it can be.
- Encourage working in small steps they can see and check, committing working states so a bad experiment can be undone, and keeping the original behavior as a reference until the replacement matches it.

## 4. If the person is a maintainer or co-owner

They are not a beginner. Skip the onboarding hand-holding and ask what they need.
When reviewing or accepting changes:

- Treat changes to the ECU assembly, EPROM emulation timing, or power circuitry as safety-critical.
  Minimum bar: bench-tested, test harness run, and the author says what was and was not tested on real hardware.
- Check that the maintainer status line and the owner list are still true. If ownership has changed, offer to update them.
- Make sure more than one person holds admin access to the repository and any accounts or hosting the project depends on.

## 5. Setting up the environment

Setup is long and error-prone.
It is your job, not the person's, except for the steps marked HUMAN below.

### 5a. Find out where you are running

Run `uname -a` (or check `$WSL_DISTRO_NAME`) before anything else.

- **Native Linux, or WSL2 (Linux inside Windows):** continue to 5b.
- **Native Windows (PowerShell, no Linux shell):** the firmware build cannot run here. Do not try to port it.
  The person must first set up WSL2. This is the one part you cannot do for them.
  Give them these steps, one at a time, in plain language:
  1. HUMAN: open PowerShell as Administrator, run `wsl --install`, reboot.
  2. HUMAN: in VS Code, install the "WSL" extension, then use "WSL: New WSL Window".
  3. HUMAN: in that WSL window, install the Claude extension again (extensions are per-environment), then open a terminal and run `git clone <repo URL>`, then File > Open Folder on the clone.
  4. Start a new Claude session in that window and resume from 5b. Your old session does not carry over.

  Windows 11 22H2 or later is required (check with `winver`).
  Windows 10 is unsupported: the device finds the server by UDP broadcast, which needs WSL's mirrored networking (see BUILDING.md).
  Tell a Windows 10 user plainly; do not try to work around it.
  A person who only wants the visualizer or decoder can skip WSL: `uv` runs natively on Windows.
  Ask which goal they have (section 1) before sending them through WSL.

### 5b. Install what the goal needs

1. Read BUILDING.md. Check what is already installed before installing anything (versions matter: Pico SDK, arm-none-eabi-gcc, m68hc11 binutils, uv, OpenOCD, picotool).
2. Install only what the person's goal needs. Visualizer or decoder only: uv alone is enough (`uv run tools/logtools/viz/viz.py <file.h5>`).
3. Some builds (m68hc11 binutils, OpenOCD) compile from source and take a long time.
   Say so up front, with a rough estimate, so a long silence is not mistaken for a hang.
4. After each stage, verify with a command, not by assumption. Exit checks:
   - Python environment: `uv run` of a tool works.
   - Full firmware build: `cd build && ninja` completes.
   - Hardware present (only if they have it): `uv run tests/runner.py --preflight-only` (see the hardware-test-run skill).
5. If a step fails: explain why it failed before trying anything else.
   After two failures on the same approach, stop and report (AGENTS.md section 5).
6. Make every step safe to repeat.
   If the session ends (reboot, closed window), the next session must be able to find out what is already done by checking the machine, not by remembering.
   Do not rely on your memory of an earlier session.
7. You may keep a short progress note outside the repository (for example `~/.umod4-setup-notes.md`) listing what you believe is done.
   Treat it as a hint only: verify each item on the machine before trusting it, and keep it out of git.

### 5c. Things only the person can do (HUMAN steps)

Ask for these one at a time, say exactly what to do and what they should see, then wait.

- **sudo password.** You cannot type it. Never suggest passwordless sudo.
  Tell the person exactly what the command will do, have them run it in their own terminal, and ask them to tell you when it finishes.
  Batch all the apt installs into one command so they enter the password once.
- **Windows-side USB (WSL2 only).**
  The debug probe and ap_proxy must be forwarded into WSL with usbipd-win.
  Installing usbipd-win and the first `usbipd bind` need an Administrator PowerShell.
  Give the exact commands and the expected device IDs (see tests/harness/usb_ids.py).
- **Physical actions:** plugging a board in or out, holding BOOTSEL, grounding SPARE2 for EP debugging, soldering.
  After each, verify it took effect (the device appears, the LED state is as expected) before continuing.
- **Accounts and decisions:** GitHub login, forking, choosing a device name or WiFi credentials. Never invent these.

### 5d. Permission prompts

The person will be asked to approve each command you run. A beginner may click through without reading.
Keep commands small and single-purpose, say in plain language what each does before you run it, and never bundle a risky command (`sudo`, `rm -rf`, anything that writes outside the project) into a long chain where it will not be noticed.

### 5e. Prerequisite checker

`scripts/doctor` does not exist yet.
It should be a read-only checker that reports which prerequisites are installed (with versions), whether the build artifacts exist, and what is still missing, so that an agent can pick up a half-finished setup instead of starting over.
Until it exists, do these checks by hand (section 3b of AGENTS.md).
Check whether it exists before you start; when it does, run it first and again after each stage.

## 6. Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
