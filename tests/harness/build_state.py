"""
Build-state checks: verify that build artifacts are not older than their sources.

Each artifact is checked against the source trees that feed into it.  A stale
artifact means the developer modified source but did not rebuild before running
tests — results against that artifact cannot be trusted.

On stale: fatal() by default, warn() if --allow-stale was passed.

Only git-tracked files are considered.  Untracked files (scratch headers,
experiments) are excluded to avoid false positives: if an untracked file is
genuinely included by a tracked source, CMake's .d dependency tracking ensures
a rebuild will update the artifact timestamp, so the check will read "fresh"
after any real build.
"""

import os
import subprocess


def _newest_mtime(roots, project_root):
    """
    Return (mtime, path) of the newest git-tracked file under roots.
    roots are paths relative to project_root (files or directories).
    Returns (0.0, None) if git fails or no tracked files are found.
    """
    result = subprocess.run(
        ["git", "ls-files", "--"] + roots,
        capture_output=True, text=True, cwd=project_root
    )
    newest_mtime = 0.0
    newest_path  = None
    for rel in result.stdout.splitlines():
        rel = rel.strip()
        if not rel:
            continue
        fpath = os.path.join(project_root, rel)
        try:
            mtime = os.path.getmtime(fpath)
        except OSError:
            continue
        if mtime > newest_mtime:
            newest_mtime = mtime
            newest_path  = fpath
    return newest_mtime, newest_path


# Each entry: (test_name, display_label, artifact_path_relative, [source_roots_relative])
# Roots are relative to project root.
_ARTIFACTS = [
    (
        "wp_uf2",
        "WP.uf2",
        os.path.join("build", "WP", "WP.uf2"),
        ["WP/src", "lib", "SwdReflash/src", "cmake", "CMakeLists.txt"],
    ),
    (
        "ep_uf2",
        "EP.uf2",
        os.path.join("build", "EP", "EP.uf2"),
        ["EP/src", "lib", "cmake", "CMakeLists.txt"],
    ),
    (
        "wpusbboot",
        "WpUsbBoot",
        os.path.join("build", "WpUsbBoot", "WpUsbBoot"),
        ["WpUsbBoot", "cmake", "CMakeLists.txt"],
    ),
]


def run_all(results, project_root, allow_stale=False):
    for test_name, label, rel_artifact, source_roots in _ARTIFACTS:
        artifact = os.path.join(project_root, rel_artifact)

        if not os.path.isfile(artifact):
            # Existence is already a fatal in preflight; just note it here.
            results.passed(test_name, f"{label} not present — checked in preflight")
            continue

        art_mtime            = os.path.getmtime(artifact)
        src_mtime, src_path  = _newest_mtime(source_roots, project_root)

        if src_mtime <= art_mtime:
            results.passed(test_name, label)
        else:
            age_s  = int(src_mtime - art_mtime)
            rel    = os.path.relpath(src_path, project_root)
            detail = f"{label} is {age_s}s older than {rel} — rebuild before trusting results"
            if allow_stale:
                results.warn(test_name, detail)
            else:
                results.fatal(test_name, detail)
