---
name: debug-protocol
description: Stop-and-assess protocol for debugging sessions that are not converging. Use when an approach has failed twice, when the user asks "what is going on?" or "are you sure?", when the user writes in capitals or swears, before trying any hardware or firmware approach you are not certain about, or when the user types /debug-protocol.
---

# Debug protocol: do not flail

Flailing does no good.
These rules come from CLAUDE.md and AGENTS.md section 5. This skill makes them something the user can invoke by name.

## What flailing looks like

- Making overly confident predictions after multiple failures.
- Trying the same thing with slight variations.
- Trying random things "just in case".
- Attempting more than 2 fixes without understanding the root cause.

## Before trying a hardware or firmware approach you are not certain about

State your confidence level explicitly, using one of these:

- "I know this from docs / disassembly / the code" (and say which).
- "I believe this but am not sure."

Also state what you do NOT know that could make it fail.
Hardware guesses have been wrong repeatedly in this project.

Reproduce the failure before fixing it.
A fix for a failure you have not seen cannot be confirmed.

## After a failure

- Explain WHY it failed before proposing anything new.
- If you cannot explain it, say so: "I don't know why that didn't work."
- Do not propose a variant of the same approach if you cannot explain the previous failure.

## Hard stop: two failures on the same approach

Stop and write this summary for the user. Do not try a third variant.

1. **What I tried.**
2. **What I know for certain, and what I guessed.** Keep these in separate lists.
3. **The open question.**
4. **Whether I recommend continuing or stopping**, and why.

## Signals to stop and listen

- The user asks "what is going on?" or "are you sure?": give an honest assessment of where things stand. Do not defend the current approach, and do not slide into the next attempt.
- The user writes in all capitals or swears: stop, and do not keep charging ahead.
- The user ends a message with "Discuss.": talk it over first. Do not edit anything until they say to.

## After a reflash

Confirm the new build is actually running before trusting any result. See the reflash-verify skill.

## Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
