# Plan: Standardize On uv For Build And Release

Status: PLAN, reviewed in discussion, NOT STARTED.
Written 2026-10-03, revised 2026-10-04.
Items marked **(verify)** are guesses or inferences that Phase 0 must confirm before the work depends on them.

## Progress

* **Phase P: done 2026-10-04.**
  Backup run, `.venv/` excluded in `backup-umod4`, local tag `pre-uv` = `48e7596` (not pushed).
* **Phase 0: passed 2026-10-04** (uv 0.11.27, Python 3.12.3, all in scratch dirs, no repo changes):
  * 0.1: `uv sync` pruned real packages but left hand-made `Logsyms/` and `LogMeta/` dirs in site-packages alone.
  * 0.2: the candidate dependency groups install; none of flask-cors, JSON_minify, zstandard, ordered-set or nuitka is present.
    With flat Logsyms (via `PYTHONPATH`), `decodelog.py` converted `log_3.um4` (1,316,522 records) to HDF5 whose 68 datasets and attributes are identical to those produced by today's `build/.venv`.
    The generated `Logsyms.py` and `LogMeta.py` match the build's copies (the only difference is one comment line recording the generation command).
    `viz.py` imports headlessly with the decoder available; all nine `tools/server` modules import; bson, mmh3, serial and requests import.
  * 0.3: CMake's `find_package(Python3 3.10 ...)` logic and the `lib/python3.12/site-packages` path work with a uv-made venv.
    (Configuring the real subprojects with it is the Phase 2 gate.)
  * 0.4: `uv` is found under `bash -lic`, via `~/.profile`.
    Not tested: the Windows Task Scheduler / `wsl.exe` launch itself, which can only be checked by a real nightly run (Phase 2 or 3 gate).
* **Phase 1: done 2026-10-04** (committed on `uv-migration`, `6dbaab4`).
  * `Logsyms.py` and `LogMeta.py` are generated straight into `build/python_packages/` (`PYTHON_GEN_DIR`).
    Nothing is installed into the venv any more; the `__init__.py` re-export packages are gone.
  * `EP/CMakeLists.txt` and `decodelog.py` read from that directory; `decodelog.py` no longer walks to `build/.venv`.
  * Verified with the old venv copies moved aside: incremental build OK, EP `tx_encoder.c` byte-identical, `decodelog.py` HDF5 identical to the pre-change baseline (68 of 68 datasets), `-L` override and the missing-directory error message work, viz imports.
  * Full hardware suite: 69 of 69 passed in one continuous run.
  * The gate's "clean build from an empty `build/`" was not done: it re-clones picotool and reinstalls it into `~/.local`, and wipes `build/test_reports`.
    It stays on the list for the Phase 2 gate, where the venv itself moves.
* **Phase 2: done 2026-10-04** (committed on `uv-migration`).
  * `pyproject.toml` (dependency groups: build, decode, viz, server, test; `default-groups = "all"`), `uv.lock` (30 packages, 499 lines), `requires-python = "==3.12.*"`, `.venv/` in `.gitignore`.
  * The top-level `CMakeLists.txt` finds uv and runs `uv sync --locked` instead of creating `build/.venv` and running 17 `pip install` commands; the venv is now `<repo>/.venv`.
    `PYTHON_DEFS` now carries only `Python3_EXECUTABLE` and `PYTHON_GEN_DIR` (`VENV_PATH` and `PYTHON_VERSION` were unused by any subproject).
  * Updated: `run_server.sh`, `flash_ap_proxy`, `nightly_run.sh` (`uv run`), `preflight.py` (adds a `uv_present` check, looks for `.venv`), error messages in three suites, and the docs (BUILDING.md has a new uv section replacing the python3-venv step).
  * Gate, all passed: genuine clean build from an empty `build/` and `.venv` (configure and ninja exit 0, 45 s; artifact sizes identical to before); `uv run decodelog.py` HDF5 identical to the pre-migration baseline (68 of 68 datasets); environment found from any directory; full hardware suite via `uv run`: 70 of 70 in one continuous run.
  * Not done: running `nightly_run.sh` by hand. The nightly task has been broken for months, per robin, so it is not a gate.
  * Not touched on purpose: `tests/runner.py` (its docstring still shows `build/.venv/bin/python3`) and `tests/suites/test_provisioning.py`, which carry uncommitted harness edits (see below).
  * Lesson: configure with `-G Ninja` (VS Code's `cmake.generator`); without it the littlefs sub-build fails with a confusing "Ninja is GNU Make" error.
* **Phase 4: script written 2026-10-05; tested on Linux and on native Windows 11 (PowerShell on robin's PC: uv present, no Python, so uv downloaded Python 3.12 and the packages). Open: choose the sample log (see below).**
  * `tools/make_viz_bundle` (stdlib only, run with `uv run tools/make_viz_bundle`) writes `build/dist/umod4-viz-<date>-<hash>[-dirty].zip` (2.2 MB, 33 files).
    It takes its file list from `git ls-files` (tracked `.py`/`.yaml` under `tools/logtools/decoder` and `viz`), generates `Logsyms.py` with `h2py.py` (no firmware build needed), and adds the root `pyproject.toml` and `uv.lock`, the sample log and a README built from `tools/TRY_ME.txt`.
  * Decision: the bundle ships the root `pyproject.toml`/`uv.lock` (one lock to maintain) and the recipient runs with `--only-group viz`; no trimmed pyproject of its own.
  * Tested after unpacking in a scratch directory with no repo or `build/` anywhere nearby: `uv run --project . --only-group viz` installs 10 packages (flask, sqlalchemy and pyserial absent; Python 3.12; `.venv` 371 MB); `decodelog.py` converts the sample log to HDF5 identical to the pre-migration baseline (68 of 68 datasets); `viz.py` starts and stays up with Qt in offscreen mode, no tracebacks.
  * Windows run (robin, 2026-10-05): install, Python 3.12 download, viz start, conversion of the sample log and graphing all worked.
    The HDF5 file produced on Windows is byte-identical to the Linux one, so nothing in the pipeline is platform-dependent.
  * Finding: the graphs of the shipped sample, `tools/logtools/test_data/log_3.um4` (committed 2025-12-08), look wrong, and it is the **log**, not the tool.
    Its cranking RPM is a median of 73 (about 1/4 of the 300-500 a real engine shows, matching the HC11 timer-prescaler bug signature in the no-start notes, fixed 2026-03-26), and it has 61,466 back-to-back RPM jumps of more than 5x.
    `log_4.um4` (from the July test bundle) shows a median cranking RPM of 309 and no such jumps, and robin confirmed it graphs sensibly on Windows.
    The exact cause of log_3's problems is not proven; it is a poor sample either way.
    It remains useful as regression data for the decoder (decoder output is identical before and after the migration).
  * Privacy: both logs contain GPS position tracks (log_3 is already public in the repo history, since 2025-12-08).
    **Open decision for robin:** what the bundle ships as its sample.
    Options: keep log_3 (looks broken); commit log_4 (publishes another ride trace); ship a sanitized sample (convert a good log to HDF5 and blank the GPS datasets); ship no sample; or keep the sample out of git and add it by hand when building a bundle.
    The bundle script, `TRY_ME.txt` and `RELEASING.md` currently refer to `log_3.um4`.
  * `tools/RELEASING.md`'s visualizer section now describes this flow; no GitHub Actions, no upload step.
* **Phase 5: done 2026-10-05.**
  * Deleted: `.github/workflows/visualizer-release.yml` (the `.github/` directory is gone), `tools/logtools/viz/requirements.txt`, `tools/server/requirements.txt`, the PyInstaller entries in `.gitignore`.
  * `decodelog.py`: removed the dead Nuitka/frozen-binary branch.
    The module-level `import Logsyms` is kept on purpose: a release bundle has `Logsyms.py` next to `decodelog.py` and no `build/` directory, so it depends on that import.
    Comments rewritten to say so.
    `viz.py`: two Nuitka comments fixed.
  * Docs: removed the user-guide section that described diagnostic scripts that do not exist, the `RELEASING.md` section about a nonexistent `build-release.sh`, and the Nuitka and `pip install -r requirements.txt` references in the server docs.
  * Gate (so far): a grep for nuitka, pyinstaller, requirements.txt and the dead script names finds nothing outside this plan, `uv.lock` and `doc/uv-migration-baseline-freeze.txt` (a historical record of the old environment, kept as is); dev-tree decode identical to the baseline (68 of 68 datasets); the missing-Logsyms error message is clear; a freshly built bundle decodes identically and runs the viz with no errors; CMake reconfigures and builds with the files gone; full hardware suite 70 of 70.
  * Tags and branch (robin's decisions, 2026-10-05): all 24 local-only `viz-v*` tags were deleted, and the stale remote branch `origin/fix/pyinstaller-hook` was deleted on GitHub (no pull requests were attached).
    That branch held the failed PyInstaller-packaging experiment of 2025-12-09 (24 commits with messages such as "this is painful"); its only non-experiment content, deleting the old JavaScript viewer, had already been done on `main` in `522b3f7`.
    No list of the deleted tags was kept: once the branch was gone those commits became unreachable and git discards them after a couple of weeks.
    GitHub now has only `main`, `ignition-noise-bench-test` and `feature/multichannel-rtt`, and no tags.
* **Provisioning is intermittently flaky (found while running the Phase 1 gate; not caused by this migration).**
  Two failure modes were seen at `prov_set_wifi`:
  `ERR request failed` (the WP applied the config and rebooted, but the HTTP response was lost: the WP powers off the WiFi chip ~300 ms after queuing the reboot), and `Timeout (10s)` (the config was NOT applied; cause unknown).
  A harness change that tolerates a lost response is kept as an **uncommitted** edit to `tests/suites/test_provisioning.py` while the timeout mode is investigated.
  Phase commits must stage files by name so that edit stays out of them.

## Goals

* Stop forgetting to activate the venv: tools run with `uv run`, from anywhere inside the repo.
* A reproducible Python environment: a committed `pyproject.toml` and `uv.lock`, replacing about 17 unpinned `pip install` calls in CMake.
* An environment that survives `rm -rf build`.
* Retire the Nuitka and PyInstaller packaging, which was large, slow and fragile.
  Releases follow the model of the July 2026 `TRY_ME` bundle: testers install uv and run the sources.
* A cleaner build: generated Python (Logsyms) no longer gets copied into the venv's site-packages.

## Decisions Made (2026-10-04, robin)

1. The venv lives at the **repo-root `.venv`**.
   Docs must say that a truly clean build means deleting `.venv` as well as `build/`.
2. **No GitHub Actions** unless a workflow is only a thin, tag-triggered call to the bundle script.
3. **uv is a developer prerequisite.**
4. Nuitka and PyInstaller go away.
5. The local `backup-umod4` script stays a **manual** step.

## Still Open (decide before or during the work)

* **LogMeta:** DECIDED for this migration: **keep it** (see "Background: Why LogMeta Exists").
  It is generated as a flat module next to Logsyms.
  Whether to ever wire it into the Python tools, or delete it, is a separate decision that this migration does not force.
* **Python version pin:** DECIDED 2026-10-04: `requires-python = "==3.12.*"` (no separate `.python-version` file: with an exact pin it would only duplicate this line).
  The original 3.10 floor was accidental (nothing needed it).
  A floor with no ceiling makes `uv.lock` record wheels for every future Python too: `>=3.12` gave 30 packages / 1,013 lines, `==3.12.*` gives 30 / 499, from the original 36 / 1,538.
  For a tools repo nobody depends on, pinning the one tested version is the right convention, and it means testers run what was tested.
  The pin does not make `.venv` big (about 390 MB, 233 MB of it PySide6); a Python that uv has to download is stored once per machine, outside `.venv`.
  Documented in `pyproject.toml` and BUILDING.md.
* **Merge points** (see "How The Work Is Merged").

## Baseline Record (what "before" looks like)

* Repo: `main` = `origin/main` = `48e7596` (2026-10-04).
* Hardware suite: all suites passed on 2026-10-04, in three runs (provisioning and config migration, then the rest).
  The only failures were a stale WiFi password and a missing SD card, both fixed.
* Old package versions: `doc/uv-migration-baseline-freeze.txt` (`pip freeze` of `build/.venv`, 32 packages, Python 3.12.3).
* Untracked lab notes (`doc/investigations/`) and all repo state backed up with `backup-umod4` on 2026-10-04.
* Known unrelated bug: `tests/runner.py --preflight-only` ends with an `UnboundLocalError` ([runner.py:543](../tests/runner.py)).

## Facts This Plan Rests On

### Python environment today

* CMake creates `build/.venv`, pip-installs packages, and passes `Python3_EXECUTABLE` (and for EP, `PYTHON_SITEPACKAGES`) to the subprojects picotool, tools, ecu, EP and ap_proxy ([CMakeLists.txt:104-155](../CMakeLists.txt)).
* Hard-coded `build/.venv` paths: `tools/server/run_server.sh`, `tools/flash_ap_proxy`, `tests/nightly_run.sh`, `tests/harness/preflight.py`, and a directory walk in `tools/logtools/decoder/decodelog.py`.
  The rest are docs and error messages: 43 references in 18 tracked files.
* The viz runs its decode subprocess with `sys.executable`, so it uses the viz's own Python.
  (This answers an earlier open question.
  That is the only subprocess call I checked.)
* The nightly test is launched from Windows Task Scheduler via `bash -lic`.
  `uv` lives in `~/.local/bin`, which `~/.profile` already puts on PATH.
  **(verify)** that it is found in that context.

### Dependencies actually in use

* Used by code: pymongo (`bson`), mmh3, numpy, h5py, pandas, pyqtgraph, PySide6, PyYAML, flask, werkzeug, sqlalchemy, requests, pyserial.
* Declared but never imported by any tracked code: **JSON_minify** (only mentioned in CLAUDE.md), **flask-cors** (no mention anywhere except CMake), **zstandard** and **ordered-set** (only in `tools/server/requirements.txt`), **nuitka** (bundling only).
  **(verify)** none is needed transitively or loaded dynamically before dropping them.

### Logsyms / LogMeta

* `lib/inc/log_ids.h` -> `h2py.py` -> `Logsyms.py`, and `gen_logmeta.py` -> `LogMeta.py`, staged in `build/python_packages/` and copied into the venv's `site-packages` ([tools/CMakeLists.txt:148-153](../tools/CMakeLists.txt)).
* Consumers of Logsyms: `decodelog.py` (import, with a fallback that walks up to `build/.venv` and builds a `lib/pythonX.Y/site-packages` path) and the EP build ([EP/CMakeLists.txt:167](../EP/CMakeLists.txt)).
* The test bundle already proves a flat layout works: `Logsyms.py` sits next to `decodelog.py` and `import Logsyms` succeeds.
* `LogMeta.py` has no Python importer (see "Background: Why LogMeta Exists").

### Background: Why LogMeta Exists

Recovered from git history on 2026-10-04.
The design note `LOGID_METADATA_PLAN.md` was committed 2026-03-02 (`68cba1f`) and deleted 2026-03-27 (`9945cd1`, docs cleanup).
Claude transcripts from March no longer exist; the oldest remaining one is from 2026-08-10.

* **Problem:** the live ECU data page on the WP (`ecu_live.html`) showed raw hex log IDs and raw integers.
* **Idea:** define a human-readable name, units and conversion for each log ID once, as metadata next to the LOGID definitions in `log_ids.h`, and use it everywhere: the WP live page and the Python tools.
* **What got built:**
  * WP side (finished, 2026-03-17): `_NAME` / `_UNITS` macros in `log_ids.h`, a table in `lib/src/log_meta.c`, and `ecu_live.html` now shows names and converted values.
  * Python side (partly finished): `gen_logmeta.py` generates `LogMeta.py` (a dict of log ID -> name, units), and the conversion formulas were moved into `tools/logtools/decoder/log_convert.py`, with the same function names as the C versions.
  * Never wired up: nothing imports `LogMeta.py`.
    The visualizer kept its own names and units in `stream_config.yaml`.
* So the idea "names and converted values instead of hex, defined once" is done for the WP page and unfinished for the Python tools.

### Packaging leftovers

* `.github/workflows/visualizer-release.yml`: Nuitka builds for Windows, Linux and macOS, triggered by `viz-v*` tags or manually.
* Frozen-binary special cases in `decodelog.py` and `viz.py`.
* `nuitka` pip-installed by CMake and listed in `tools/server/requirements.txt`.
* `tools/RELEASING.md` still describes PyInstaller; `.gitignore` lists PyInstaller output dirs.
* `tools/logtools/viz/requirements.txt` is stale (PyQt6).
* The July 2026 `TRY_ME` bundle: a 194-byte `pyproject.toml` (PySide6-Essentials, pyqtgraph, numpy, pandas, h5py, PyYAML), a `uv.lock`, `logtools/`, a pre-generated `Logsyms.py`; run with `uv run --project . logtools/viz/viz.py <log>`.
  It was assembled by hand.

## Target Design

### One root `pyproject.toml` and `uv.lock`

* A virtual (non-installable) project, with dependency groups so each consumer installs only what it needs:

| Group | Contents |
|---|---|
| `build` | pymongo, mmh3 |
| `decode` | numpy, h5py, PyYAML |
| `viz` | `decode` plus PySide6-Essentials, pyqtgraph, pandas |
| `server` | PySide6-Essentials, flask, sqlalchemy, requests |
| `test` | pyserial, requests |

* Developers sync all groups by default.
  Release bundles use only the `viz` group.
* The four dependencies listed above as unused (and nuitka) are not carried over, once Phase 0 confirms.

### Logsyms

* Generate flat modules into `build/python_packages/` (no re-export `__init__.py` packages, no copy into site-packages).
* `decodelog.py` tries a normal import, then adds `<repo>/build/python_packages` to `sys.path`.
  This replaces the `build/.venv` directory walk and the Python-version path arithmetic.
* The EP build reads `Logsyms.py` from the new directory.
* Bundles copy `Logsyms.py` next to `decodelog.py`, as the test bundle did.

### CMake

* Replace the venv-creation block and the pip lines with a find-uv check and `uv sync` (fail clearly if uv is missing).
* Keep passing `Python3_EXECUTABLE` to the subprojects, now pointing at `.venv/bin/python3`.
* Drop `PYTHON_SITEPACKAGES` once Logsyms no longer lives there.

### Running things

* `uv run tools/...` from anywhere inside the repo.
* `run_server.sh`, `flash_ap_proxy` and `nightly_run.sh` call `uv run` instead of the hard-coded interpreter.
* `preflight.py` checks for `uv` on PATH and that `uv sync --check` passes, instead of checking for a venv file.

### Release

* A bundle script (for example `tools/make_viz_bundle`) assembles the proven test-bundle layout from the repo: `viz` group `pyproject.toml` and `uv.lock`, `tools/logtools`, generated `Logsyms.py`, a README in the style of `TRY_ME.txt`.
* Output is one platform-independent zip, uploaded to a GitHub Release by hand.
  A tag-triggered workflow that only runs the script is acceptable later.
* The server follows the same pattern; no standalone executables.

## Phases

The order differs from my first draft: Logsyms is decoupled from the venv **first**, while the venv is still `build/.venv`.
Reason: `decodelog.py` finds Logsyms by walking to `build/.venv`, so moving the venv and Logsyms in the same step would break the decoder.
Phase 1 is independent of uv, shippable alone, and testable with the current environment.

Every phase ends with a **gate**.
Do not start the next phase until the gate passes.

### Phase P: Preparation (small, before any migration commit)

* **P1.** Run `backup-umod4` once more after this plan and the baseline freeze exist (both are untracked).
* **P2.** Edit `~/.local/bin/backup-umod4` to add `--exclude '.venv/'`.
  A repo-root venv would otherwise add about 500 MB to every backup.
  The script stays manual.
* **P3.** Optional: a local tag `pre-uv` on `48e7596`, as a named rollback point (not pushed).
* **Gate:** `git status` shows only the untracked notes, plan and freeze file; the backup is current.

### Phase 0: Verify assumptions (scratch directories only, no repo changes)

* **0.1** In a scratch dir, make a uv project, hand-create `site-packages/Logsyms/` and `LogMeta/` dirs, run `uv sync`, and see whether they survive.
  Relevant only if Phase 2 landed before Logsyms moved, but it explains the risk and is cheap.
* **0.2** Build the candidate `pyproject.toml` in scratch.
  Check: `decodelog.py` converts `tools/logtools/test_data/log_3.um4` to HDF5; `verify_hdf5.py` passes; `viz.py` imports headlessly (`QT_QPA_PLATFORM=offscreen`); the server modules import.
  This also confirms JSON_minify, flask-cors, zstandard, ordered-set and nuitka can be dropped.
* **0.3** Confirm the uv-created `.venv` satisfies CMake's `find_package(Python3)` block and that picotool, tools, ecu, EP and ap_proxy all configure with it.
* **0.4** Confirm `uv` is found when launched the way the nightly task launches it (`bash -lic`).
* **Gate:** every check passes or the plan is revised.
  Record the results at the top of this doc.

### Phase 1: Decouple Logsyms from the venv

* Generate flat `Logsyms.py` into `build/python_packages/`; drop the copy into site-packages.
* Change `decodelog.py` (import, then `sys.path` fallback) and `EP/CMakeLists.txt` (read from the new path).
* Keep LogMeta: generate `LogMeta.py` as a flat module in `build/python_packages/`, like Logsyms.
  (Nothing imports it today, so its only check is that it still generates.)
* **Gate:** clean build from an empty `build/`; decoder produces the same HDF5 as before for `log_3.um4` (compare with `verify_hdf5.py`, plus a diff of the decoded output against the baseline); full hardware suite passes.
* **Merge point 1** (recommended): independent and low risk.

### Phase 2: The uv environment

* Heads-up: in an *incremental* build, editing the top-level `CMakeLists.txt` makes `tests/harness/build_state.py` report `WpUsbBoot` as stale (it treats that file as a source of every subproject).
  Touch `WpUsbBoot/main.c` and run `ninja` (a genuine recompile). A clean build, as in this phase's gate, does not have the problem.

* Add `pyproject.toml`, `uv.lock`, `.gitignore` entry for `.venv`.
* CMake: `uv sync` replaces venv creation and the pip block; venv at repo-root `.venv`; update `PYTHON_DEFS`.
* Update every hard-coded `build/.venv` path in code (the four scripts and `decodelog.py`'s fallback).
* **Gate:** delete both `build/` and `.venv`, then configure and build from scratch; the full hardware suite passes; the nightly script runs to completion once by hand.

### Phase 3: Tools and tests use `uv run`

* Switch the shell scripts, `preflight.py`, `tests/nightly_run.sh` and docs to `uv run`.
* **Gate:** full hardware suite via `uv run`; the nightly task fires once on schedule and reports normally.
* **Merge point 2** (recommended): "everything works with uv".

### Phase 4: Release bundle

* Write the bundle script; test on a machine that has only uv (the Windows desktop used for the July test).
* **Gate:** a tester-style run: install uv, unzip, run, convert a log, see graphs.

### Phase 5: Remove the old mechanisms

* Delete: `.github/workflows/visualizer-release.yml`, the Nuitka and PyInstaller code and comments in `decodelog.py` and `viz.py`, `tools/logtools/viz/requirements.txt`, `tools/server/requirements.txt`, the PyInstaller `.gitignore` entries, and the Nuitka and PyInstaller text in the docs.
* Clean up the old release tags and the stale remote branch: done (see Progress).
* **Gate:** a grep for `nuitka` and `pyinstaller` finds nothing outside this plan and the history; clean build and hardware suite pass; `git tag -l 'viz-v*'` shows nothing.

### Phase 6: Docs sweep

* Update CLAUDE.md, BUILDING.md (uv as a prerequisite), GETTING_STARTED.md, Tools.md, `tests/README.md`, `tests/TEST_PLAN.md`, `tools/RELEASING.md`, `README_Visualizer*.md`, `tools/server/README.md`, and `tools/TRY_ME.txt` or its replacement.
* Update docs alongside each phase where practical, so they never describe a state that no longer exists.
* **Gate:** every command in the docs works as written when tried on a clean checkout.

## How The Work Is Merged

* One branch, `uv-migration`, cut from `main` (not from the bench branch).
  This plan and the baseline freeze are its first commit.
* Each phase is one or a few commits.
* Merge to `main` as a fast-forward at the merge points above, so `main` never holds a half-finished migration.
  If `main` moves meanwhile, merge `main` into the branch first.
* Every commit message is drafted and approved before `git commit`.
  The test runner must have been run before each merge.
* Pushing to GitHub is a separate, explicit decision each time.
  The lab notes in `doc/investigations/` never get committed.

## Rollback

* **Before merging to `main`:** `git switch main`, delete the `uv-migration` branch, `rm -rf build .venv`, and reconfigure.
  Package versions then come from pip as of that day.
  The exact old versions are in `doc/uv-migration-baseline-freeze.txt` if needed.
* **After merging to `main` but before pushing:** move the local `main` back to the `pre-uv` tag or to `48e7596`.
* **After pushing:** revert the merge with new commits (never rewrite public history).
* **Untracked lab notes:** restore from `OneDrive/Archive/umod4` (do not re-run the backup first if it might overwrite the good copy).
* `~/.umod4_test_env` holds the credentials and is not part of any backup.

## Related Idea (out of scope for this migration)

Finish the "names and converted units defined once" idea on the Python side.
For example, let the visualizer take display names and units from `log_ids.h` (via `LogMeta.py`) instead of keeping a second copy in `stream_config.yaml`.
uv does not make this easier or harder: it manages Python environments, not the sharing of definitions between C and Python.
The migration only has to avoid breaking it, which is why LogMeta is kept.

## Risks

* uv becomes a minor build prerequisite: `uv` must be on PATH, including in the nightly task's shell.
  `pyproject.toml` and its dependency groups follow Python standards, so `pip` could still read the dependency list if uv were ever unusable; only `uv.lock` is uv-specific.
* The nightly task runs in a different shell context from your terminal; `uv` must be on PATH there (checked in 0.4).
* A cross-platform `uv.lock` must resolve for Linux, Windows and macOS.
  The test bundle's lock already contained win32 markers, so this works in practice.
* The venv's old unpinned package versions are not reproducible after a rebuild (the baseline freeze mitigates this).
* Anything outside the repo that hard-codes `build/.venv` (cron entries, scheduled tasks, aliases) is invisible to this plan. **(verify)**
