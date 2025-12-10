# PyInstaller Diagnostic Tools - Complete System

This directory contains a comprehensive suite of automated diagnostic tools to systematically identify and fix PyInstaller packaging issues, eliminating the guess-and-check cycle.

## The Problem These Tools Solve

When PyInstaller builds work on Linux but fail on Windows (or vice versa), you need:
- **Evidence, not guesses** - What's actually in the bundle?
- **Runtime verification** - Do modules import successfully?
- **Build analysis** - What warnings did PyInstaller emit?
- **Cross-platform comparison** - What's different between working and broken builds?

Yesterday's issue: Trying PyInstaller options blindly without being able to verify what each change actually did.

## The Solution: Automated Diagnostic Loop

```
Build → Extract → Analyze → Compare → Fix → Verify
```

Instead of guessing, you now have tools that show you exactly what's wrong.

## Tool Overview

| Tool | Purpose | When to Use |
|------|---------|-------------|
| `diagnose_pyinstaller.sh` | Local comprehensive diagnostic | Before pushing to GitHub |
| `.github/workflows/pyinstaller-diagnostics.yml` | Cross-platform CI diagnostic | Auto-runs on push; manual trigger for testing |
| `self_diagnostic.py` | Runtime module verification | Built into app; runs at startup |
| `PYINSTALLER_DIAGNOSTICS.md` | Complete debugging guide | Reference when fixing issues |

## Quick Start

### 1. Test Locally (Before Pushing)

```bash
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh
```

Review: `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md`

**Key checks:**
- ✓ All modules show "found" in diagnostic_runtime.log
- ✓ All .pyc files present in extracted_contents.txt
- ✓ No "warning" or "not found" in build log

### 2. Test on GitHub (All Platforms)

Push your changes, or manually trigger the workflow:
1. Go to GitHub Actions → PyInstaller Diagnostics
2. Click "Run workflow"
3. Wait for completion (~5-10 minutes)
4. Download artifacts:
   - `diagnostics-Windows`
   - `diagnostics-Linux`
   - `diagnostics-macOS`
   - `comparison-report`

**Key checks:**
- Compare `diagnostic_runtime.log` across platforms
- Look for modules that work on Linux but fail on Windows
- Check `comparison_report.md` for platform differences

### 3. Add Runtime Diagnostics (Optional)

For development/debugging, enable self-checks at startup:

```python
# At top of viz.py
import self_diagnostic
if not self_diagnostic.quick_check():
    print("WARNING: Module check failed - see diagnostic output")
```

See `ENABLE_DIAGNOSTICS.md` for full integration options.

## What Each Tool Does

### Local Script: `diagnose_pyinstaller.sh`

**Actions:**
1. Builds visualizer with PyInstaller (full debug logging)
2. Builds diagnostic-only executable
3. Extracts both executables to inspect contents
4. Runs diagnostic executable to test imports
5. Analyzes build logs for warnings/errors
6. Searches extracted files for required modules
7. Generates comprehensive markdown report

**Output:** `build/pyinstaller_diagnostics/` directory containing:
- `DIAGNOSTIC_REPORT.md` - Main report with all findings
- `diagnostic_runtime.log` - Module import test results
- `pyinstaller_build.log` - Full PyInstaller build log (DEBUG level)
- `extracted_contents.txt` - Complete file listing from bundle
- `DataVisualizer_extracted/` - Extracted executable files
- `DataVisualizer.spec` - Generated PyInstaller spec file

**Time:** ~1-2 minutes

### GitHub Workflow: `pyinstaller-diagnostics.yml`

**Actions:**
1. Runs diagnostic build on Windows, Linux, macOS in parallel
2. Extracts executables on each platform
3. Tests module imports on each platform
4. Uploads platform-specific diagnostic bundles
5. Compares results across platforms in final job
6. Generates cross-platform comparison report

**Artifacts:** Available for 90 days after run
- `diagnostics-Windows` - Complete Windows diagnostic bundle
- `diagnostics-Linux` - Complete Linux diagnostic bundle
- `diagnostics-macOS` - Complete macOS diagnostic bundle
- `comparison-report` - Shows differences between platforms

**Time:** ~5-10 minutes (parallel execution)

**Trigger:**
- Automatic: On push to main when `tools/logtools/**` changes
- Manual: Actions tab → PyInstaller Diagnostics → Run workflow

### Runtime Module: `self_diagnostic.py`

**Capabilities:**
- `quick_check()` - Silent boolean check (fast)
- `run_diagnostic()` - Detailed check returning result object
- `check_and_report()` - Print full report
- Command line: `python self_diagnostic.py --verbose`

**Integration:**
- Optional - app works without it
- Useful during development
- Can be enabled in frozen apps to catch packaging bugs
- See `ENABLE_DIAGNOSTICS.md` for integration guide

## Systematic Debugging Process

Stop guessing. Follow this workflow:

### Step 1: Gather Evidence

Run local diagnostic:
```bash
./tools/diagnose_pyinstaller.sh
```

### Step 2: Identify Missing Modules

Open `build/pyinstaller_diagnostics/diagnostic_runtime.log`:
```
✓ decodelog          -> /path/to/decodelog.pyc
✗ conversions        -> NOT FOUND
```

This tells you exactly what's missing.

### Step 3: Confirm Files Not Bundled

Check `extracted_contents.txt`:
```bash
grep -i conversions build/pyinstaller_diagnostics/extracted_contents.txt
```

If nothing found → PyInstaller didn't bundle it.

### Step 4: Find Root Cause

Check `pyinstaller_build.log`:
```bash
grep -i "conversions" build/pyinstaller_diagnostics/pyinstaller_build.log
grep -i "warning" build/pyinstaller_diagnostics/pyinstaller_build.log
```

Look for:
- `WARNING: Hidden import "conversions" not found!`
- `ImportError: No module named 'conversions'`
- Module search path issues

### Step 5: Apply Targeted Fix

Based on evidence:

**If module not found during build:**
```bash
--paths=tools/logtools/decoder
```

**If module bundled as data (not importable):**
```bash
--hidden-import=conversions
```

**If submodules missing:**
```bash
--hidden-import=viz_components.config
--hidden-import=viz_components.widgets
# etc.
```

### Step 6: Verify Fix

Re-run diagnostic:
```bash
./tools/diagnose_pyinstaller.sh
```

Confirm:
- ✓ Module now shows "found" in runtime log
- ✓ File present in extracted_contents.txt
- ✓ No warnings about module in build log

### Step 7: Test Cross-Platform

Push to GitHub, wait for workflow, download artifacts.

Compare `diagnostic_runtime.log` files across platforms - should all show ✓.

## Common Issues and Solutions

See `PYINSTALLER_DIAGNOSTICS.md` for detailed troubleshooting, including:

- Module not found at runtime (but file exists)
- Module files missing from bundle
- Submodules missing (parent package found)
- Windows-specific path/separator issues
- Platform comparison strategies

## Files in This System

```
tools/
├── diagnose_pyinstaller.sh           # Local diagnostic script
├── DIAGNOSTIC_TOOLS_SUMMARY.md       # This file
├── PYINSTALLER_DIAGNOSTICS.md        # Complete debugging guide
└── logtools/viz/
    ├── self_diagnostic.py            # Runtime module checker
    └── ENABLE_DIAGNOSTICS.md         # Integration guide

.github/workflows/
└── pyinstaller-diagnostics.yml       # CI diagnostic workflow
```

## Success Metrics

You've successfully diagnosed and fixed the issue when:

1. ✓ Local diagnostic shows all modules found
2. ✓ Extracted contents includes all required .pyc files
3. ✓ Build log has no warnings about missing modules
4. ✓ All three platforms (Windows/Linux/macOS) show identical module availability
5. ✓ Actual application launches without ImportError

## Maintenance

**Keep diagnostic tools in sync with code:**

When you add new required modules to the visualizer:
1. Update `critical_modules` list in diagnostic_wrapper.py
2. Update `required_modules` list in self_diagnostic.py
3. Test that diagnostics catch the new dependency

**When diagnostic workflow fails:**

The workflow is designed to always succeed (even if it finds problems).
If the workflow itself fails:
1. Check GitHub Actions log for infrastructure issues
2. Verify pyinstxtractor-ng installation step succeeds
3. Check that extraction commands work on all platforms

## Benefits Over Yesterday's Approach

| Yesterday | Today |
|-----------|-------|
| Guess what's wrong | See exactly what's missing |
| Try random PyInstaller flags | Apply evidence-based fixes |
| No way to verify changes | Extract and inspect each build |
| Manual testing only | Automated cross-platform CI |
| Each attempt takes ~20 min | Get results from all 3 platforms in 10 min |
| No comparison between platforms | Automatic diff of Windows vs Linux |

## Getting Help

If diagnostics show problems but you're unsure how to fix:

1. Run `./tools/diagnose_pyinstaller.sh`
2. Gather these files:
   - `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md`
   - `build/pyinstaller_diagnostics/diagnostic_runtime.log`
   - `build/pyinstaller_diagnostics/extracted_contents.txt`
3. Include in your bug report/question

Having this diagnostic output makes it 10x easier for others to help you.

## Next Steps

1. **Try it now:**
   ```bash
   ./tools/diagnose_pyinstaller.sh
   ```

2. **Review the results:**
   ```bash
   cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md
   ```

3. **If issues found:**
   - Read `PYINSTALLER_DIAGNOSTICS.md` for solutions
   - Apply fixes
   - Re-run diagnostic to verify

4. **Test cross-platform:**
   - Push to GitHub
   - Check Actions → PyInstaller Diagnostics
   - Download artifacts
   - Compare results

5. **Consider adding runtime checks:**
   - Review `ENABLE_DIAGNOSTICS.md`
   - Add `self_diagnostic.quick_check()` to viz.py
   - Catches packaging bugs immediately at startup

You now have a complete diagnostic system. No more guessing!
