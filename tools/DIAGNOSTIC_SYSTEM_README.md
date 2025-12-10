# PyInstaller Diagnostic System

## What This Solves

**The Problem:** Yesterday you were stuck in a loop trying to fix Windows PyInstaller builds, guessing at solutions without being able to verify what each change did. The builds kept failing due to missing modules, but there was no systematic way to:
- See what was actually bundled in the .exe
- Verify which modules were importable at runtime
- Compare working (Linux) vs broken (Windows) builds
- Identify the root cause from build logs

**The Solution:** A complete automated diagnostic system that extracts, analyzes, and reports on PyInstaller builds, closing the feedback loop and eliminating guesswork.

## What You Now Have

### 1. Local Diagnostic Script
**File:** `tools/diagnose_pyinstaller.sh`

**What it does:**
- Builds your visualizer with PyInstaller (DEBUG logging)
- Builds a diagnostic-only executable
- Extracts both executables to inspect contents
- Runs diagnostic executable to test module imports at runtime
- Analyzes build logs for warnings/errors
- Searches for your custom modules in the bundle
- Generates comprehensive markdown report

**Run it:**
```bash
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh
```

**Output:** `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md` (start here)

**Time:** ~1-2 minutes

### 2. Existing Build Analyzer
**File:** `tools/analyze_existing_build.sh`

**What it does:**
- Analyzes any PyInstaller executable (even from GitHub Actions artifacts)
- Extracts and inspects contents
- Checks for required modules
- Generates analysis report

**Run it:**
```bash
./tools/analyze_existing_build.sh path/to/DataVisualizer.exe
```

**Use case:** Download a Windows build from GitHub Actions, analyze locally

**Time:** ~30 seconds

### 3. GitHub Actions Workflow
**File:** `.github/workflows/pyinstaller-diagnostics.yml`

**What it does:**
- Builds on Windows, Linux, macOS in parallel
- Extracts each platform's executable
- Tests runtime module availability on each platform
- Generates platform-specific diagnostic bundles
- Compares results across platforms
- Uploads all results as artifacts

**Trigger:**
- Auto: Push to main when `tools/logtools/**` changes
- Manual: GitHub Actions → PyInstaller Diagnostics → Run workflow

**Artifacts:** Available for 90 days
- `diagnostics-Windows`
- `diagnostics-Linux`
- `diagnostics-macOS`
- `comparison-report`

**Time:** ~5-10 minutes (parallel)

### 4. Runtime Self-Diagnostic Module
**File:** `tools/logtools/viz/self_diagnostic.py`

**What it does:**
- Standalone module that checks if all required modules are importable
- Can be integrated into the app startup
- Generates detailed diagnostic report
- Useful for catching packaging bugs immediately

**Run it:**
```bash
python tools/logtools/viz/self_diagnostic.py --verbose
```

**Integration:** Optional, see `ENABLE_DIAGNOSTICS.md`

### 5. Complete Documentation

* **QUICK_REFERENCE.md** - One-page cheat sheet with commands and common fixes
* **DIAGNOSTIC_TOOLS_SUMMARY.md** - Complete system overview and usage guide
* **PYINSTALLER_DIAGNOSTICS.md** - Detailed debugging walkthrough with examples
* **ENABLE_DIAGNOSTICS.md** - How to add runtime checks to the app

## How It Works

### The Diagnostic Loop

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Build with PyInstaller (DEBUG logging)                   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Extract executable with pyinstxtractor                    │
│    → See exactly what files are bundled                      │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Run diagnostic executable                                 │
│    → Test if each module can be imported                     │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Analyze build log                                         │
│    → Find warnings about missing modules                     │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. Generate report showing:                                  │
│    • Which modules are missing (✗)                           │
│    • Which files are/aren't in bundle                        │
│    • Build warnings and errors                               │
│    • Recommended fixes                                       │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. Apply targeted fix based on evidence                      │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. Re-run diagnostic to verify fix                           │
└─────────────────────────────────────────────────────────────┘
```

### Key Tools Used

* **pyinstxtractor-ng** - Extracts PyInstaller executables to inspect contents
* **PyInstaller DEBUG logs** - Shows detailed build process and warnings
* **importlib.util.find_spec** - Tests if modules are actually importable
* **Cross-platform CI** - Compares behavior across Windows/Linux/macOS

## Example: Finding and Fixing a Missing Module

### Step 1: Run Diagnostic
```bash
./tools/diagnose_pyinstaller.sh
```

### Step 2: Check Runtime Log
Open `build/pyinstaller_diagnostics/diagnostic_runtime.log`:
```
✓ decodelog                              -> /tmp/_MEI.../decodelog.pyc
✗ conversions                            -> NOT FOUND (spec is None)
✓ stream_config                          -> /tmp/_MEI.../stream_config.pyc
```

**Finding:** `conversions` module is missing!

### Step 3: Verify Not Bundled
```bash
grep -i conversions build/pyinstaller_diagnostics/extracted_contents.txt
```
Result: No matches → Confirmed not bundled.

### Step 4: Check Build Log
```bash
grep -i conversions build/pyinstaller_diagnostics/pyinstaller_build.log
```
Result:
```
WARNING: Hidden import "conversions" not found!
```

**Root cause:** PyInstaller couldn't find the module during build.

### Step 5: Apply Fix
Add to PyInstaller command:
```bash
--paths=tools/logtools/decoder \
--hidden-import=conversions
```

### Step 6: Verify Fix
Re-run diagnostic:
```bash
./tools/diagnose_pyinstaller.sh
```

Check runtime log:
```
✓ conversions                            -> /tmp/_MEI.../conversions.pyc
```

**Success!** Module now found.

## Quick Reference

### Commands
```bash
# Local full diagnostic
./tools/diagnose_pyinstaller.sh

# Analyze existing build
./tools/analyze_existing_build.sh path/to/executable

# Run self-diagnostic module
python tools/logtools/viz/self_diagnostic.py --verbose
```

### Files to Check After Diagnostic
1. `DIAGNOSTIC_REPORT.md` - Start here for summary
2. `diagnostic_runtime.log` - Which modules are importable? (✓ or ✗)
3. `extracted_contents.txt` - Is the file actually in the bundle?
4. `pyinstaller_build.log` - What warnings did PyInstaller emit?

### Common Fixes
```bash
# Module not found during build
--paths=tools/logtools/decoder

# Module bundled but not importable
--hidden-import=module_name

# All submodules
--hidden-import=viz_components.config \
--hidden-import=viz_components.widgets
```

## Benefits Over Yesterday's Approach

| Yesterday | Today |
|-----------|-------|
| ❌ Guess what's wrong | ✅ See exactly what's missing |
| ❌ Try random flags | ✅ Apply evidence-based fixes |
| ❌ No way to verify | ✅ Extract and inspect each build |
| ❌ Manual testing only | ✅ Automated cross-platform CI |
| ❌ Each test takes 20 min | ✅ All platforms in 10 min (parallel) |
| ❌ Can't compare platforms | ✅ Automatic Windows vs Linux diff |
| ❌ Blind guessing loop | ✅ Systematic debug process |

## Integration with Your Workflow

### Before Pushing to GitHub
```bash
# Quick check
./tools/diagnose_pyinstaller.sh
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md

# If all ✓, push with confidence
git push
```

### After GitHub Actions Build
```bash
# Download artifacts from Actions tab
# Analyze any platform-specific build
./tools/analyze_existing_build.sh ~/Downloads/DataVisualizer.exe

# Compare reports across platforms
```

### During Development
```bash
# Add self-diagnostic to app (optional)
# See ENABLE_DIAGNOSTICS.md
```

## Success Criteria

Your build is good when:

- [x] `diagnostic_runtime.log` shows all modules with ✓
- [x] `extracted_contents.txt` contains all `.pyc` files for your modules
- [x] `pyinstaller_build.log` has no warnings about your modules
- [x] All three platforms show identical module availability
- [x] Application launches without ImportError

## When to Use Each Tool

| Scenario | Tool | Why |
|----------|------|-----|
| Before pushing changes | `diagnose_pyinstaller.sh` | Catch issues locally |
| Analyzing GitHub artifact | `analyze_existing_build.sh` | Quick check of downloaded .exe |
| Testing all platforms | GitHub Actions workflow | Parallel cross-platform testing |
| Debugging app startup | `self_diagnostic.py` | Runtime module verification |
| Quick lookup | `QUICK_REFERENCE.md` | Common commands and fixes |

## Files Created

```
tools/
├── diagnose_pyinstaller.sh              ← Main local diagnostic script
├── analyze_existing_build.sh            ← Analyze any executable
├── DIAGNOSTIC_SYSTEM_README.md          ← This file
├── DIAGNOSTIC_TOOLS_SUMMARY.md          ← Complete guide
├── PYINSTALLER_DIAGNOSTICS.md           ← Detailed debugging
├── QUICK_REFERENCE.md                   ← Cheat sheet
└── logtools/viz/
    ├── self_diagnostic.py               ← Runtime module checker
    └── ENABLE_DIAGNOSTICS.md            ← Integration guide

.github/workflows/
└── pyinstaller-diagnostics.yml          ← Cross-platform CI

Updated:
└── tools/README_Visualizer_User_Guide.md ← Added developer section
```

## Next Steps

1. **Try it now:**
   ```bash
   cd ~/projects/umod4
   ./tools/diagnose_pyinstaller.sh
   ```

2. **Review results:**
   ```bash
   cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md
   ```

3. **If issues found:**
   - Consult `QUICK_REFERENCE.md` for common fixes
   - Read `PYINSTALLER_DIAGNOSTICS.md` for detailed solutions

4. **Test cross-platform:**
   - Push to GitHub (or manually trigger workflow)
   - Download diagnostic artifacts
   - Compare platform results

5. **For production:**
   - Consider adding `self_diagnostic.quick_check()` to app startup
   - See `ENABLE_DIAGNOSTICS.md` for integration

## Support

If you encounter issues or have questions about the diagnostic tools:

1. Run the diagnostic and gather output
2. Include these files in your question:
   - `DIAGNOSTIC_REPORT.md`
   - `diagnostic_runtime.log`
   - `extracted_contents.txt`

The detailed output makes it much easier to identify problems.

---

**Bottom Line:** You now have a complete, automated system to diagnose PyInstaller issues systematically. No more guessing—just run the diagnostic, read the report, apply the evidence-based fix, and verify success.
