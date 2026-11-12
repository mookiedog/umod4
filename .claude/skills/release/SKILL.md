---
name: release
description: Drive a umod4 release, either a test release (a rehearsal that is never made public) or a real one. Use when the user asks to make, prepare, test, rehearse or publish a release, tag a version, or build the release folder or tools zip.
---

# Making a release

`RELEASING.md` is the single source for the steps, the checklist, the file formats and troubleshooting.
Read it first, every time, and follow it; do not work from memory of an earlier session.
This skill adds only what an agent must do on top of it: which kind of release this is, which steps you do yourself, and where you must stop and wait for Robin.

## First: ask which kind of release

Ask this before running anything, as its own question, and do not guess:

- **Test release**: a rehearsal of the process. Nothing public is produced.
- **Real release**: the one riders will download.

Also get the version.
A test release uses a throwaway version such as `v0.1.0-test1`, so it can never be mistaken for the real `v0.1.0`.
For a real release, Robin chooses the version (semantic versioning, see RELEASING.md); suggest one, never decide it.
Say back what you understood ("test release v0.1.0-test1") and wait for a yes.

## What is the same for both

- `git status` must be clean and the branch must be `main`, pushed. Check it.
- Run `uv run tools/make_release --version <version>`, adding `--previous <folder>` when an earlier real release exists (ask where it is kept; never guess).
- Read the changed/unchanged table in `RELEASE_NOTES.md` and compare it with `git diff --name-only <previous commit> HEAD`, not just with what the script says.
- Check the zip contents against the list in RELEASING.md.
- Check `release.json` and `SHA256SUMS` agree with the files.

## Test release

The point is to find problems in the process, so report every surprise, and propose a fix to `RELEASING.md` or the script for each one (edit only after Robin agrees).

- Bypass flags (`--allow-dirty`, `--no-build`) are allowed if needed, but say so; the output will be marked REHEARSAL, which is expected.
- The bike gate is optional: ask whether Robin wants to flash the release folder's files this time or only rehearse the steps. Never present a skipped hardware test as passed.
- Publishing: at most a **draft** GitHub release, and only if Robin asks for that part to be rehearsed. Never a published release and never a pre-release. Say before each `gh` or `git push` command what it will do.
- Always finish with cleanup, and show Robin the exact commands before running them:
  - `gh release delete <tag> --cleanup-tag --yes` (only for a release that this session made)
  - `git tag -d <tag>` locally
  - delete the test release folder under `build/dist/` only if Robin says so
- Confirm afterwards with `gh release list` and `git tag` that nothing test-related is left.

## Real release

No bypass flags. If the output says REHEARSAL, stop: it cannot be published.
Walk the checklist in RELEASING.md in order and tick items off out loud.

Stop and wait for Robin, with a plain statement of what you need, at each of these.
You never decide any of them, and you never report one as done unless Robin has said so:

1. **The version.**
2. **The hardware test runner.** Ask whether it has been run (AGENTS.md section 5). It needs the bench hardware and a board erase; you can run it only if Robin says go.
3. **The bike gate.** If any firmware file is "changed" (every file is in a first release), the exact `EP.uf2` and `WP.uf2` from the release folder must be flashed and bench- and bike-tested by Robin. Never say a firmware file is safe for the bike (AGENTS.md section 4); say what was and was not tested. After the flash, check the server or `/api/info` shows the builds that `release.json` lists.
4. **The tools zip on a clean machine** (Windows Sandbox is available): the server window opens, the visualizer opens the sample ride, the map shows.
5. **The release notes.** Draft "What changed" from `git log` since the previous release, in plain language for riders, say plainly if a rider has to do something, then let Robin edit it.
6. **Publishing.** Before anything is pushed or published, show the exact commands (`git tag`, `git push origin <tag>`, `gh release create ... --draft`) and get an explicit go-ahead for each. Prefer creating a **draft** and letting Robin read it as a visitor would, then publish only on his word.

After publishing:

- Remind Robin to copy the release folder to a safe place outside the repository; it is `--previous` next time.
- Check whether GETTING_STARTED.md (or any other document) points riders at download names or places that changed, and say so.
- Check that the published release has every file in the folder, and that the downloaded checksums match `SHA256SUMS`.

## Rules that apply throughout

- Never push, tag, publish or delete anything on GitHub without Robin's explicit go-ahead for that specific action.
- Never stop processes or delete folders on Robin's PC without asking (this includes `C:\temp` and the sandbox's folders).
- If something fails twice, stop and report (AGENTS.md section 5); do not try variations.
- If RELEASING.md and the script disagree, say so and ask which is right; do not silently pick one.
- At the end, if you hit a situation this skill does not cover, propose a specific edit to this file and wait for approval.
