# Release Process Guide

A umod4 release is **one** release containing everything a person needs: the tools (server, visualizer and decoder in one zip) and the firmware files.
There is one version number for the whole thing, such as `v0.1.0`, which is also the git tag and the GitHub release name.

Nothing in this process publishes anything by itself.
The script assembles a folder, and a person decides whether to attach its files to a GitHub release.

## Release checklist

Tick these off in order.
The sections below explain each item.

- [ ] On `main`, with everything committed and pushed: `git status` shows nothing.
- [ ] Choose the version (one number for the tools and the firmware, for example `v0.2.0`).
- [ ] Run `uv run tools/make_release --version v0.2.0 --previous <the previous release's folder>` (leave out `--previous` for the first release). The output must not say REHEARSAL.
- [ ] Read the table in `RELEASE_NOTES.md`: a firmware file must say "unchanged" only if you changed nothing that feeds it.
- [ ] The hardware test runner passes: `uv run tests/runner.py` (see the hardware-test-run skill). It tests the source, using the build in `build/`. If it complains that a file is older than a `CMakeLists.txt`, see Troubleshooting.
- [ ] If any firmware file is marked "changed" (every file is "new" in a first release): flash `EP.uf2` and `WP.uf2` **from the release folder** to the bike, check that the server shows the builds listed in `release.json`, then bench- and bike-test. Never flash from `build/` for this: every build re-stamps the WP, so a file from `build/` is not the file that will be published.
- [ ] Test the tools zip on a clean Windows profile: the server window opens, the visualizer opens the sample ride, and the map shows. A throwaway Windows user account does it (see "Testing the tools zip"). Do the full test for the first release and whenever the launchers, `pyproject.toml`, `uv.lock` or the tools `README.txt` changed; otherwise unzip it into a new folder on your own PC and check that both programs start.
- [ ] Write "What changed" in `RELEASE_NOTES.md`.
- [ ] Copy the release folder to a safe place outside the repository (for example `~/umod4-releases/`): it is the `--previous` for the next release.
- [ ] Tag the commit and publish by hand: `git tag v0.2.0`, `git push origin v0.2.0`, then GitHub, Releases, draft from that tag, paste the notes and attach every file in the folder. Read the draft once as a visitor would before publishing.
- [ ] If the download names or places changed, update the documents that point riders at the release (GETTING_STARTED.md).

## What a release contains

| File | What it is |
| --- | --- |
| `umod4-tools-<version>.zip` | the server, the visualizer and the decoder, plus launchers and a sample ride; the recipient needs only [uv](https://docs.astral.sh/uv/) |
| `EP.uf2` | the EP firmware (the UM4 ECU image is built into it); installed by the server over WiFi |
| `WP.uf2` | the WP firmware; installed by the server over WiFi, or by BOOTSEL drag-and-drop on a blank WP |
| `WP_partition.uf2`, `WP_erase.uf2` | the two extra files for the first install of a blank WP (BOOTSEL drag-and-drop) |
| `UM4.bin` | the data-logging ECU image, for the image library (it is also built into `EP.uf2`) |
| `SHA256SUMS` | a checksum for every file above |
| `release.json` | the same information for programs to read: per file the checksum, the build time, and whether it changed |
| `RELEASE_NOTES.md` | a draft of the release notes; the file table is filled in, "what changed" is for a person to write |

## "Changed" means changed, not "built again"

Every build stamps a new build time into `EP.uf2` and `WP.uf2`, so rebuilding identical source gives a different file.
A checksum comparison would therefore call everything changed.
Instead, when you give the script the previous release, it asks git whether any *input* of each file changed between the previous release's commit and now:

- If nothing changed, the previous release's file is reused byte for byte (the very file that was already tested and flashed) and is marked **unchanged**.
- If something changed, the freshly built file is used and is marked **changed**.

The input lists in `tools/make_release` are deliberately generous, so the worst mistake the script can make is to say "changed" when it did not matter, never the reverse.
For a rider this is real information: when the EP (which runs the engine) is unchanged, there is nothing to reflash.

## release.json

`release.json` describes the release for programs, such as a future "which of my devices need updating" check in the server.
It has the release's `version`, `date`, the `git` commit it was assembled from, the `previous` release if there was one, and a `files` list.
Each file has:

- `name`, `kind`, `description`, `size` and `sha256`
- `built_from_commit`: the commit its source came from (for a reused file this is an earlier commit than the release's)
- `first_released_in`: the release in which these exact bytes first appeared
- `changed`: whether it changed since the previous release (present only when a previous release was given)
- `build_time` (firmware and `UM4.bin` only): the build time, for people
- `device_reports` (firmware and `UM4.bin` only): what a device reports about itself once this file is installed, in the same shape as the device's own `/api/info`

For example, the EP is identified by two things, because its own build time does not change when only the ECU image inside it changes, and the device reports the two separately:

```json
"device_reports": {
  "ep_version": {"BT": "2026-11-10 09:48:17"},
  "ecu_build": {"GH": "pre-uv-30-g45d4bdd", "BT": "2026-11-11 16:03:07"}
}
```

**How to match a file against a device.**
Every key in the manifest's object must equal what the device reports for that key.
Keys that the manifest does not have are ignored, and a key that the device does not report means "unknown", not "no match".
So if the firmware later reports more (a version number, a git hash), a new key in the manifest is enough, and older readers keep matching on the keys they know.
A device reports the EP's and the WP's build times in the compiler's format ("Nov 10 2026 09:47:12": the WP reformats the EP's time to match its own), while the manifest stores them in ISO form ("2026-11-10 09:47:12"), so a reader must normalise every build time before comparing.

**How the format may change.**
Adding a new optional key does not change `"schema": 1`, and a reader must ignore keys it does not know.
Removing or renaming a key, or changing what one means, increases `schema`, and a reader that does not know that number must refuse the file and say so.

## Making a release

1. **Finish and commit the changes**, on `main`, with a clean tree.

2. **Run the script.**
   For the first release:

   ```bash
   cd ~/projects/umod4
   uv run tools/make_release --version v0.1.0
   ```

   For later releases, point it at the previous release's folder so that it can mark and reuse unchanged files:

   ```bash
   uv run tools/make_release --version v0.2.0 --previous ~/umod4-releases/umod4-release-v0.1.0
   ```

   The script runs the build first (`ninja` in `build/`), refuses to continue on a tree with uncommitted changes, refuses an ECU image that was built from an uncommitted tree (`-dirty`), and checks that the firmware files agree with each other.
   The result is `build/dist/umod4-release-<version>/`.
   Copy that folder somewhere that survives deleting `build/`: you need it as `--previous` next time.
   Read the file table in its `RELEASE_NOTES.md`.

   Run the script **before** the bike test, not after.
   Every build stamps a new build time into `WP.uf2`, even when no source changed, so a `WP.uf2` taken from `build/` before the script ran is not the file the script publishes.
   The files to test are the ones in the release folder.

3. **Run the hardware test runner**: `uv run tests/runner.py`.
   It tests the source, using the build in `build/`, and it flashes the WP on the bench board itself.
   If it stops with "is N s older than CMakeLists.txt", see Troubleshooting.

4. **Test the firmware, from the release folder.**
   If a firmware file is marked "changed", or this is the first release, flash `EP.uf2` and `WP.uf2` from the release folder to the bike, using the server's Reflash EP and Reflash WP buttons.
   The server's device panel should then show the EP and WP builds that are listed in `release.json`.
   Then bench-test, and ride.
   Files marked "unchanged" are the earlier files that were already tested, so there is nothing to flash for them.

5. **Test the tools zip on a clean Windows profile.**
   See "Testing the tools zip" below for how.
   Unzip it, follow `README.txt` from STEP 1, start the server and check that its window opens, then start the visualizer on the sample ride and check that graphs and the map appear.
   The test does not mean much in a profile that already has uv or the umod4 source tree, because that hides missing files.
   Only one server can run at a time: it needs UDP port 54549 and says so if another server already has it.

6. **Write the release notes.**
   Open `RELEASE_NOTES.md` and fill in "What changed".
   Say plainly if a rider has to do something.
   If the log format changed, say which firmware and tools versions go together; the tools in a release always read logs from that release's firmware and from older firmware.

7. **Publish, by hand.**
   Tag the commit (`git tag v0.1.0` and `git push origin v0.1.0`), then on GitHub go to Releases, draft a new release for that tag, paste in the notes, and attach every file in the release folder.

A run that bypasses a safety check (for a rehearsal, with `--allow-dirty` or `--no-build`) marks `release.json` and the notes with `"rehearsal"`.
Never publish a rehearsal.

### Version numbering

Use semantic versioning (MAJOR.MINOR.PATCH):

- **MAJOR**: breaking changes, for example logs that old tools cannot read
- **MINOR**: new features, backward compatible
- **PATCH**: bug fixes only

Because tools and firmware share one version, a fix to the visualizer alone is still a new release.
The "changed / unchanged" column in the notes is what tells a rider that the firmware files are the same as before.

## The tools zip

`tools/make_tools_bundle` builds the tools zip, and `tools/make_release` calls it.
You can also run it alone, for a quick test bundle:

```bash
uv run tools/make_tools_bundle
```

This writes `build/dist/umod4-tools-<date>-<git hash>.zip`, marked `-dirty` if any bundled file has uncommitted changes.

The zip keeps the repository's own layout, because the server's "launch visualizer" button finds the visualizer by relative path.
It contains:

- `tools/server`: the server's runtime `.py` files (no tests, no dead code, no developer docs)
- `tools/logtools/viz` and `tools/logtools/decoder`: the tracked `.py` and `.yaml` files
- `tools/logtools/decoder/Logsyms.py`, generated on the spot from `lib/inc/log_ids.h` (no firmware build is needed)
- `tools/logtools/test_data/sample_ride.um4`, the sample ride
- `pyproject.toml` and `uv.lock`
- `run_server.cmd`, `run_viz.cmd` (Windows, double-click) and `run_server.sh`, `run_viz.sh` (Linux), taken from `tools/bundle/`
- `README.txt`: the instructions for the recipient, taken from the template `tools/bundle/README.txt`

The launchers run `uv run --only-group server ...` and `uv run --only-group viz ...`.
The `--only-group` part installs just that program's packages and leaves out the other group and the test packages.

## Testing the tools zip

Use a throwaway Windows user account on your own PC: its profile has no uv, no environment variables and no leftovers, which is what a rider's PC looks like.

1. Close any running umod4 server.
2. In Settings, Accounts, Other users, add a local account (no Microsoft account) and make it an Administrator, as most riders are.
3. Sign in to it, extract the zip, and follow `README.txt` from STEP 1.
4. Afterwards, sign out and delete the account with "Delete account and data". That removes everything the test created, including uv and its downloads.

**What this does not prove:**

- The Windows Firewall popup. A machine that has the rule `umod4 Server UDP Check-in` (UDP 54549, any program) never shows it, so the account test cannot either. Whether a rider's default choice in the popup is enough is untested.
- Machine-wide security settings such as Smart App Control, which may block the Python that uv downloads.
- A machine that has never run the umod4.

Windows Sandbox is not a good substitute on current Windows 11: it has no Notepad, and on 2026-11-12 its Application Control policy blocked uv's downloaded Python (`os error 4551`).

## Troubleshooting

**The test runner stops with "X is N s older than CMakeLists.txt, rebuild before trusting results":**

- The runner compares file times, and git changes them (switching branches, merging), so it can complain about a file that is really up to date, and `ninja` will not rebuild it because nothing changed.
- Touch one of that sub-project's own source files and rebuild: for example `touch WpUsbBoot/main.c`, then `ninja` in `build/`.
- This changes only a file time, not the contents, so `git status` stays clean.

**`tools/make_release` says the build failed:**

- The last lines of the build output are shown; fix the error and run it again.

**`tools/make_release` says UM4.bin was built from an uncommitted tree:**

- The ECU image carries the `git describe` of the tree it was built from, and `-dirty` means that tree had uncommitted changes.
- Commit, then rebuild the ECU image (`touch ecu/src/UM4.S`, then `ninja` in `build/`) and run the script again.

**`tools/make_release` says a file is unchanged but that release's file is missing:**

- `--previous` must be the folder of the previous release, including the firmware files, not only its `release.json`.

**`tools/make_tools_bundle` fails:**

- Run it from inside the repository, with uv: `uv run tools/make_tools_bundle` (the script asks git for its file list, so git must be installed)
- "expected file is missing" means a file the bundle needs was moved or deleted; the message names it

**The recipient's first run of a program is slow, or does not start:**

- The first run of each program downloads Python 3.12 (if the machine has none) and that program's packages (about 300 MB for the visualizer), so it needs an internet connection
- uv itself must be installed first; `README.txt` in the bundle explains how

**GitHub upload fails:**

- Check the file size is under 2 GB (the largest file is `WP.uf2`, about 3.4 MB)
- Try uploading through the web interface instead
