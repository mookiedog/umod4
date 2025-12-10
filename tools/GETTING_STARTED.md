# Getting Started with PyInstaller Diagnostics

## TL;DR - What to Run

```bash
# Install the dependency (one-time only)
python3 -m pip install pyinstxtractor-ng

# Run the full diagnostic (do this before pushing to GitHub)
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh

# View results
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md
```

That's it! Everything else is automatic.

---

## Detailed Workflow

### Step 1: First Time Setup

Install the extraction tool (one-time only):

```bash
python3 -m pip install pyinstxtractor-ng
```

This tool lets the scripts extract and inspect PyInstaller executables.

### Step 2: Test Your Build Locally

Before pushing to GitHub, test locally:

```bash
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh
```

**What happens automatically:**
1. ✓ Checks dependencies (installs if missing)
2. ✓ Builds visualizer with PyInstaller
3. ✓ Builds diagnostic test executable
4. ✓ Extracts both executables
5. ✓ Runs module import tests
6. ✓ Analyzes build logs
7. ✓ Generates comprehensive report

**Time:** ~1-2 minutes

**Output location:** `build/pyinstaller_diagnostics/`

### Step 3: Review the Results

```bash
# Main report (start here)
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md

# Or open in your editor
code build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md
```

**Look for:**
- ✓ symbols = modules found (good!)
- ✗ symbols = modules missing (problem!)

### Step 4: Fix Issues (if any)

If the report shows missing modules, check [QUICK_REFERENCE.md](QUICK_REFERENCE.md) for common fixes.

**Example fix for missing `decodelog` module:**

In `.github/workflows/visualizer-release.yml`, add:
```bash
--hidden-import=decodelog
```

### Step 5: Verify the Fix

Re-run the diagnostic:
```bash
./tools/diagnose_pyinstaller.sh
```

Confirm all modules now show ✓ in the report.

### Step 6: Push to GitHub

Once local diagnostic passes, push your changes:
```bash
git add .
git commit -m "Fix PyInstaller packaging"
git push
```

### Step 7: GitHub Actions (Automatic)

**The workflow runs automatically** when you push changes to `tools/logtools/**` files.

**Or trigger manually:**
1. Go to GitHub → Actions tab
2. Click "PyInstaller Diagnostics"
3. Click "Run workflow" → "Run workflow"

**Wait ~5-10 minutes** for all three platforms to build and test.

### Step 8: Download Cross-Platform Results (Optional)

1. Go to the completed workflow run
2. Scroll down to "Artifacts" section
3. Download:
   - `diagnostics-Windows` - Windows diagnostic bundle
   - `diagnostics-Linux` - Linux diagnostic bundle
   - `diagnostics-macOS` - macOS diagnostic bundle
   - `comparison-report` - Cross-platform comparison

4. Review each platform's `diagnostic_runtime.log` to compare

---

## When Windows Build Fails: Analyzing the Windows Build on Linux

**The scenario:** Your Linux build works locally, but the Windows build from GitHub Actions is broken.

**The solution:** Download and analyze the Windows build on your Linux machine to see what's different!

### Option A: Review the Diagnostic Artifact (Fastest)

The GitHub Actions workflow already extracted and analyzed the Windows build for you:

```bash
# 1. Download diagnostics-Windows.zip from GitHub Actions artifacts
# 2. Extract it
unzip ~/Downloads/diagnostics-Windows.zip -d windows-diagnostics/

# 3. See what failed on Windows
cat windows-diagnostics/diagnostic_runtime.log     # Which modules are missing?
cat windows-diagnostics/DIAGNOSTIC_REPORT.md       # Full Windows report

# 4. Compare Windows vs your working Linux build
diff windows-diagnostics/extracted_contents.txt \
     build/pyinstaller_diagnostics/extracted_contents.txt

# This shows exactly which files are in Linux but missing from Windows!
```

### Option B: Analyze the Windows .exe Directly

If you have the actual Windows `.exe` (from releases or artifacts):

```bash
# Extract and analyze the Windows executable on Linux
./tools/analyze_existing_build.sh ~/Downloads/DataVisualizer.exe

# View results
cat ~/Downloads/DataVisualizer.exe_analysis/ANALYSIS_REPORT.md
```

**Key insight:** You can extract and inspect Windows .exe files on Linux using `pyinstxtractor`. This lets you see exactly what's bundled in the broken Windows build without needing Windows!

### Example: Finding the Difference

```bash
# Your working Linux build shows:
$ grep decodelog build/pyinstaller_diagnostics/extracted_contents.txt
[FILE] decodelog.pyc (1234 bytes)

# Broken Windows build shows:
$ grep decodelog windows-diagnostics/extracted_contents.txt
(no matches)

# Conclusion: decodelog.pyc is missing from Windows build!
# Fix: Add --hidden-import=decodelog to build command
```

---

## Troubleshooting

### "pyinstxtractor not found" error

Install it:
```bash
python3 -m pip install pyinstxtractor-ng
```

### "Permission denied" when running scripts

Make them executable:
```bash
chmod +x tools/diagnose_pyinstaller.sh
chmod +x tools/analyze_existing_build.sh
```

### Script fails during PyInstaller build

Check that you have all dependencies:
```bash
pip install pyinstaller PyQt6 pyqtgraph numpy pandas h5py
```

### Want more details

See the comprehensive guides:
- [QUICK_REFERENCE.md](QUICK_REFERENCE.md) - Quick commands
- [DIAGNOSTIC_TOOLS_SUMMARY.md](DIAGNOSTIC_TOOLS_SUMMARY.md) - Full system overview
- [PYINSTALLER_DIAGNOSTICS.md](PYINSTALLER_DIAGNOSTICS.md) - Detailed debugging

---

## Summary: The Simple Workflow

```
┌─────────────────────────────────────────────────────────┐
│ 1. Install pyinstxtractor-ng (one-time)                 │
│    python3 -m pip install pyinstxtractor-ng             │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│ 2. Run local diagnostic                                  │
│    ./tools/diagnose_pyinstaller.sh                      │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│ 3. Review report                                         │
│    cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md     │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│ 4. If all ✓ → Push to GitHub                            │
│    If any ✗ → Apply fixes and repeat from step 2       │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│ 5. GitHub Actions runs automatically (or trigger manual)│
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│ 6. Download artifacts to see all platforms              │
│    (Optional - only if you want cross-platform check)   │
└─────────────────────────────────────────────────────────┘
```

**No manual extraction, no manual analysis - it's all automated!**

---

## Quick Reference Card

```bash
# Essential commands
python3 -m pip install pyinstxtractor-ng          # One-time setup
./tools/diagnose_pyinstaller.sh                  # Main diagnostic
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md # View results

# Optional commands
./tools/analyze_existing_build.sh path/to/exe    # Analyze downloaded build
python tools/logtools/viz/self_diagnostic.py     # Test module directly
```

**That's all you need to know to get started!**
