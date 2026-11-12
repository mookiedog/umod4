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
- Run `uv run tools/make_release --version <version>`, adding `--previous ~/umod4-releases/umod4-release-<previous version>` when an earlier real release exists (check that the folder is there; if not, ask where it is, never guess).
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
3. **The bike gate.** If any firmware file is "changed" (every file is in a first release), the exact `EP.uf2` and `WP.uf2` from the release folder must be flashed and bench- and bike-tested by Robin. Never say a firmware file is safe for the bike (AGENTS.md section 4); say what was and was not tested. Flash in the order WP, EP, WP (RELEASING.md, step 4, explains why) and check the server or `/api/info` against `release.json` after each flash. Ask what the device ran before (identical files, or the previous release's builds) and say so in the notes.
4. **The tools zip on a clean Windows profile** (a throwaway user account; RELEASING.md, "Testing the tools zip", says how and what it cannot prove): the server window opens, the visualizer opens the sample ride, the map shows. Ask whether the launchers, `pyproject.toml`, `uv.lock` or the tools README changed since the last release (`git diff --name-only <previous commit> HEAD -- tools/bundle pyproject.toml uv.lock`); if none did, an unzip-and-start check on Robin's own PC is enough. Windows Sandbox does not work for this on current Windows 11.
5. **The release notes.** Draft "What changed" from `git log` since the previous release, in plain language for riders, say plainly if a rider has to do something, then let Robin edit it.
6. **Keep a copy of the release folder** (you do this yourself, no need to wait for Robin, but tell him): after the notes are final and before anything is published, copy it with `cp -r build/dist/umod4-release-<version> ~/umod4-releases/` (create `~/umod4-releases/` if it is missing; if the target exists, stop and ask, never overwrite), then check the copy with `sha256sum -c SHA256SUMS`. It is `--previous` for the next release, and `build/` can be deleted at any time. A test release does not need this.
7. **Publishing.** Before anything is pushed or published, show the exact commands (`git tag`, `git push origin <tag>`, `gh release create ... --draft`) and get an explicit go-ahead for each. Prefer creating a **draft** and letting Robin read it as a visitor would, then publish only on his word.

After publishing:

- Check whether GETTING_STARTED.md (or any other document) points riders at download names or places that changed, and say so.
- Check that the published release has every file in the folder, and that the downloaded checksums match `SHA256SUMS`.

## Writing "What changed"

The reader is a rider or tinkerer asking "should I update, and do I have to do anything?".
They do not care about our internal commits.
Write the draft yourself, show it, and let Robin edit it; never publish text he has not seen.

**Inputs** (read all three):

1. The commits since the previous release: the previous release's `release.json` names its commit, so use `git log <that commit>..HEAD`. Our commit messages have category headers (`WP:`, `Tools:`, `Docs:`) and a `Verified:` line that says what was and was not exercised.
2. The changed/unchanged table in `RELEASE_NOTES.md`, which says whether a rider has to reflash the EP or the WP at all.
3. For every firmware file marked "changed", `git diff --stat <previous commit> HEAD -- <its input folders>`, so you can say what actually changed in it. A file can be "changed" only because a build script or path changed; say so plainly, and do not suggest a rider needs it.

**How to write it:**

- Lead with what a rider must do: "reflash the WP", "nothing to do for the EP", "old logs still open".
- Group by what the reader cares about: firmware (EP, WP), tools (server, visualizer, decoder), documents. Leave out purely internal work (test harness, refactors, build scripts) unless a rider would notice it.
- Plain sentences, one thing per line, no commit hashes, no jargon without a few words of explanation.
- Do not invent. Every line must trace to a commit or to the table. If a commit message is unclear, ask Robin instead of guessing.
- Keep the "Tested" part short: one line for what passed (hardware test suite, platform), one line for what the claims are limited to (Robin's own bike and board), and, when it applies, one line "Not yet ridden with this build". That last line is the one that matters for safety; never write that firmware is safe for the bike. Do not list every thing that was not tested: only Robin needs that, so put the detailed list in your report to him, not in the notes.
- If the log format changed, say which firmware and tools versions go together.

**Every release after the first** starts its "What you need to do" with this block, unchanged, when the tools zip changed (it is the same text as "UPGRADING" in `tools/bundle/README.txt`; keep the two in step):

> **Upgrading the tools**
>
> 1. Close the umod4 server and the visualizer if they are running.
> 2. Unzip the new zip. It makes a new folder with the version in its name, next to your old one. Do not unzip it over the old folder.
> 3. Start the server from the new folder (`run_server.cmd` on Windows).
> 4. When you are happy with it, delete the old folder. Your device list and logs are not stored there, so nothing is lost.
>
> You do not need to install uv again.

**The first release has no previous release.** Do not list the history.
Write instead: what this release is, what is in it, how to start (link GETTING_STARTED.md), a short "Tested" part (see above) and nothing more.

## Rules that apply throughout

- Never push, tag, publish or delete anything on GitHub without Robin's explicit go-ahead for that specific action.
- Never stop processes or delete folders on Robin's PC without asking (this includes `C:\temp` and the sandbox's folders).
- If something fails twice, stop and report (AGENTS.md section 5); do not try variations.
- If RELEASING.md and the script disagree, say so and ask which is right; do not silently pick one.
- At the end, if you hit a situation this skill does not cover, propose a specific edit to this file and wait for approval.
