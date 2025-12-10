# PyInstaller Diagnostics Guide

This guide explains how to diagnose PyInstaller packaging issues systematically, avoiding the guess-and-check cycle.

## The Problem

When PyInstaller builds fail to include required modules (especially on Windows), you need to:
1. **See what was actually bundled** - not guess
2. **Test module availability at runtime** - not assume
3. **Compare working vs broken builds** - identify differences
4. **Read build logs systematically** - catch warnings early

## Automated Tools

### 1. Local Diagnostic Script (Fastest)

Run this on your local machine before pushing to GitHub:

```bash
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh
```

**What it does:**
- Builds the visualizer with PyInstaller
- Builds a diagnostic-only executable
- Extracts the executable to see bundled files
- Runs the diagnostic executable to test module imports at runtime
- Analyzes build logs for warnings/errors
- Generates a comprehensive report

**Output location:** `build/pyinstaller_diagnostics/`

**Key files to check:**
- `DIAGNOSTIC_REPORT.md` - Summary with all findings
- `diagnostic_runtime.log` - Shows which modules are found/missing at runtime
- `extracted_contents.txt` - Complete list of bundled files
- `DataVisualizer_extracted/` - The actual extracted executable contents

### 2. GitHub Actions Workflow (Cross-Platform)

The workflow `.github/workflows/pyinstaller-diagnostics.yml` runs automatically on:
- Push to main branch (when tools/logtools/** changes)
- Manual trigger via GitHub Actions UI

**What it does:**
- Builds on Windows, Linux, and macOS simultaneously
- Extracts each platform's executable
- Tests runtime module availability on each platform
- Compares results across platforms
- Generates platform-specific and comparison reports

**Viewing results:**
1. Go to GitHub Actions tab
2. Click on the workflow run
3. Download artifacts:
   - `diagnostics-Windows` - Windows diagnostic bundle
   - `diagnostics-Linux` - Linux diagnostic bundle
   - `diagnostics-macOS` - macOS diagnostic bundle
   - `comparison-report` - Cross-platform comparison

## How to Read Diagnostic Results

### Step 1: Check Runtime Module Availability

Open `diagnostic_runtime.log` and look for the "MODULE AVAILABILITY CHECK" section:

```
✓ decodelog                              -> /path/to/decodelog.pyc
✓ conversions                            -> /path/to/conversions.pyc
✓ viz_components                         -> /path/to/viz_components/__init__.pyc
✗ some_module                            -> NOT FOUND (spec is None)
```

**Good:** All modules show ✓ with a valid path
**Bad:** Any module shows ✗ - this is your problem!

### Step 2: Verify Files Were Bundled

Open `extracted_contents.txt` and search for your module files:

```bash
grep -i "decodelog" extracted_contents.txt
grep -i "conversions" extracted_contents.txt
grep -i "viz_components" extracted_contents.txt
```

**Expected:** You should see `.pyc` files for each module
**Problem:** If the file isn't in the extracted contents, PyInstaller didn't bundle it

### Step 3: Check Build Log

Open `pyinstaller_build.log` and search for:

```bash
# Warnings about your modules
grep -i "warning.*decodelog" pyinstaller_build.log

# Module not found errors
grep -i "not found" pyinstaller_build.log

# Import errors
grep -i "ImportError" pyinstaller_build.log
```

Common warnings that indicate problems:
- `WARNING: Hidden import "X" not found!`
- `WARNING: Cannot find module "X"`
- `ImportError: No module named 'X'`

### Step 4: Inspect the Spec File

Open `DataVisualizer.spec` and check the `Analysis()` section:

```python
a = Analysis(
    ['path/to/viz.py'],
    pathex=[],  # Should this include tools/logtools?
    binaries=[],
    datas=[  # Are your files here?
        ('stream_config.py', '.'),
        ('decodelog.py', '.'),
        # ...
    ],
    hiddenimports=[],  # Should your modules be here?
    # ...
)
```

**Things to check:**
- Are your modules in `datas`?
- Should they be in `hiddenimports` instead?
- Is `pathex` empty when it should include your source directories?

## Common Issues and Fixes

### Issue 1: Module Not Found at Runtime (but file exists)

**Symptom:** File exists in extracted contents but runtime check shows ✗

**Cause:** File bundled as data (not Python module)

**Fix:** Add to `hiddenimports`:
```bash
--hidden-import=decodelog \
--hidden-import=conversions \
--hidden-import=viz_components
```

### Issue 2: Module Files Missing from Bundle

**Symptom:** Extracted contents doesn't include the `.pyc` files

**Cause:** PyInstaller can't find the modules during build

**Fix:** Add to module search path:
```bash
--paths=tools/logtools \
--paths=tools/logtools/decoder \
--paths=tools/logtools/viz
```

### Issue 3: Submodules Missing

**Symptom:** Parent package found, but submodules (viz_components.config, etc.) missing

**Cause:** Package doesn't have `__init__.py` or PyInstaller doesn't traverse it

**Fix:**
1. Ensure `viz_components/__init__.py` exists
2. Add explicit hidden imports for each submodule:
```bash
--hidden-import=viz_components.config \
--hidden-import=viz_components.widgets \
--hidden-import=viz_components.utils \
# etc.
```

### Issue 4: Windows-Specific Problems

**Symptom:** Linux build works, Windows build missing modules

**Cause:** Path separator differences or case sensitivity

**Fix:**
1. Check `--add-data` uses correct separator (`:` for Windows in GitHub Actions, even though `;` is Windows native)
2. Verify paths exist from Windows build perspective
3. Use `--paths` with absolute paths resolved at build time

## Debugging Workflow

Follow this systematic process instead of guessing:

```
1. Run local diagnostic script
   └─> Review DIAGNOSTIC_REPORT.md

2. If modules missing at runtime:
   └─> Check extracted_contents.txt
       ├─> Files present but not importable?
       │   └─> Add to --hidden-import
       └─> Files not present?
           └─> Check build log for warnings
               ├─> "not found" warnings?
               │   └─> Add to --paths
               └─> Import errors?
                   └─> Check dependencies

3. Test fix locally
   └─> Re-run diagnostic script
       └─> Verify all modules show ✓

4. Push to GitHub
   └─> Check cross-platform workflow
       └─> Download artifacts
           └─> Compare platform results
               └─> If Windows still broken:
                   └─> Check Windows-specific separator/path issues
```

## Manual Diagnostic Commands

If you need to debug an existing `.exe` or binary:

### Extract executable contents:
```bash
pip install pyinstxtractor-ng
python -m pyinstxtractor DataVisualizer.exe
ls -R DataVisualizer.exe_extracted/
```

### Analyze with pyi-archive_viewer (interactive):
```bash
pyi-archive_viewer DataVisualizer.exe
# Commands:
#   O PYZ-00.pyz    - Open Python archive
#   X               - Extract all
#   Q               - Quit
```

### Check what's in the PYZ archive:
```bash
python -c "
import sys
sys.path.insert(0, 'DataVisualizer.exe_extracted')
from PYZ-00_pyz import PYZArchive
pyz = PYZArchive('DataVisualizer.exe_extracted/PYZ-00.pyz')
for name in sorted(pyz.toc.keys()):
    print(name)
" | grep -E "(decodelog|conversions|viz_components)"
```

## Comparison with Working Build

If Linux works but Windows doesn't:

```bash
# Extract both
python -m pyinstxtractor dist/DataVisualizer          # Linux
python -m pyinstxtractor dist/DataVisualizer.exe      # Windows

# Compare file lists
find DataVisualizer_extracted -name "*.pyc" | sort > linux_modules.txt
find DataVisualizer.exe_extracted -name "*.pyc" | sort > windows_modules.txt
diff linux_modules.txt windows_modules.txt

# The diff shows what's in Linux but missing from Windows
```

## Success Criteria

You've fixed the issue when:

1. ✓ Diagnostic runtime log shows all modules found
2. ✓ Extracted contents includes all required `.pyc` files
3. ✓ Build log has no warnings about missing modules
4. ✓ Comparison report shows all platforms bundle the same modules
5. ✓ Actual application runs without ImportError

## Tools Reference

| Tool | Purpose | Installation |
|------|---------|--------------|
| `pyinstxtractor-ng` | Extract PyInstaller executables | `pip install pyinstxtractor-ng` |
| `pyi-archive_viewer` | Interactive archive inspection | Included with PyInstaller |
| Our diagnostic script | Automated comprehensive check | `./tools/diagnose_pyinstaller.sh` |
| GitHub workflow | Cross-platform automated testing | Auto-runs on push |

## Need Help?

1. Run the diagnostic script
2. Review the generated `DIAGNOSTIC_REPORT.md`
3. Check the relevant section above based on your symptoms
4. If still stuck, include these files in your issue:
   - `DIAGNOSTIC_REPORT.md`
   - `diagnostic_runtime.log`
   - `extracted_contents.txt`
   - Relevant sections from `pyinstaller_build.log`
