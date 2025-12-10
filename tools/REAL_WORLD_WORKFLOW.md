# Real-World Workflow: Fixing Windows Build Issues from Linux

## The Problem

You're developing on Linux. Your local builds work fine. But when GitHub Actions builds for Windows, the `.exe` is broken due to missing modules. How do you debug this without having Windows?

## The Solution: Remote Diagnostic + Local Analysis

```
┌──────────────────────────────────────────────────────────────┐
│ YOUR LINUX MACHINE (Development)                             │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  1. Test local Linux build                                   │
│     ./tools/diagnose_pyinstaller.sh                          │
│     ✓ All modules found                                      │
│                                                               │
│  2. Push to GitHub                                           │
│     git push                                                 │
│                                                               │
└───────────────────────┬──────────────────────────────────────┘
                        │
                        │ (triggers)
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ GITHUB ACTIONS (Cloud - Windows Runner)                      │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  3. Windows: Build DataVisualizer.exe                        │
│  4. Windows: Extract DataVisualizer.exe                      │
│  5. Windows: Test module imports                             │
│     ✗ decodelog NOT FOUND                                    │
│     ✗ conversions NOT FOUND                                  │
│  6. Windows: Generate diagnostic report                      │
│  7. Windows: Upload diagnostics-Windows.zip artifact         │
│                                                               │
└───────────────────────┬──────────────────────────────────────┘
                        │
                        │ (download)
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ YOUR LINUX MACHINE (Analysis)                                │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  8. Download diagnostics-Windows.zip                         │
│                                                               │
│  9. Compare Windows vs Linux                                 │
│     diff windows-diagnostics/extracted_contents.txt \        │
│          build/pyinstaller_diagnostics/extracted_contents.txt      │
│                                                               │
│     Result: decodelog.pyc present in Linux, absent in Windows│
│                                                               │
│ 10. Apply fix to GitHub workflow                             │
│     Add: --hidden-import=decodelog                           │
│                                                               │
│ 11. Push fixed workflow                                      │
│     git push                                                 │
│                                                               │
│ 12. Verify Windows build now works                           │
│     Download new diagnostics-Windows.zip                     │
│     ✓ decodelog found                                        │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

## Concrete Example

### Scenario: Yesterday's Problem

**Symptom:** Windows .exe from GitHub Actions fails with ImportError

**What you were doing:** Guessing at PyInstaller flags, pushing, waiting 20 min, repeat

**What you should do now:**

#### Step 1: Verify Local Build Works (Baseline)

```bash
cd ~/projects/umod4
./tools/diagnose_pyinstaller.sh
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md
```

**Expected:** All modules show ✓ on Linux

#### Step 2: Push and Let GitHub Actions Test Windows

```bash
git push
```

Wait 5-10 minutes for workflow to complete.

#### Step 3: Download Windows Diagnostic Results

1. Go to GitHub → Actions → Latest workflow run
2. Scroll to Artifacts section
3. Download `diagnostics-Windows.zip`
4. Extract it:

```bash
unzip ~/Downloads/diagnostics-Windows.zip -d ~/windows-diag/
```

#### Step 4: See What Failed on Windows

```bash
cat ~/windows-diag/diagnostic_runtime.log
```

Output shows:
```
✗ decodelog      -> NOT FOUND (spec is None)
✗ conversions    -> NOT FOUND (spec is None)
```

**Now you know exactly what's missing!**

#### Step 5: Confirm Files Not in Windows Bundle

```bash
grep -i decodelog ~/windows-diag/extracted_contents.txt
# No results = file not bundled

grep -i decodelog ~/projects/umod4/build/pyinstaller_diagnostics/extracted_contents.txt
# Shows file is present in Linux build
```

**Conclusion:** decodelog.py bundled on Linux, missing on Windows

#### Step 6: Check Windows Build Log for Clues

```bash
grep -i decodelog ~/windows-diag/pyinstaller_build.log
```

Likely shows:
```
WARNING: Hidden import "decodelog" not found!
```

**Root cause:** PyInstaller on Windows can't find the module

#### Step 7: Apply Platform-Specific Fix

Edit `.github/workflows/visualizer-release.yml`:

```yaml
- name: Build with PyInstaller (Windows/Linux)
  if: runner.os != 'macOS'
  run: |
    python -m PyInstaller --onefile --windowed \
      --add-data "tools/logtools/viz/stream_config.py:." \
      --add-data "tools/logtools/viz/stream_config.yaml:." \
      --add-data "tools/logtools/viz/viz_components:viz_components" \
      --hidden-import=decodelog \              # ← ADD THIS
      --hidden-import=conversions \            # ← AND THIS
      --name "DataVisualizer" \
      --clean \
      tools/logtools/viz/viz.py
```

#### Step 8: Verify Fix

```bash
git add .github/workflows/visualizer-release.yml
git commit -m "Fix Windows build: add hidden imports for local modules"
git push
```

Wait for GitHub Actions, download new `diagnostics-Windows.zip`:

```bash
unzip ~/Downloads/diagnostics-Windows.zip -d ~/windows-diag-fixed/
cat ~/windows-diag-fixed/diagnostic_runtime.log
```

Now shows:
```
✓ decodelog      -> /tmp/_MEI.../decodelog.pyc
✓ conversions    -> /tmp/_MEI.../conversions.pyc
```

**Success!** Windows build now works.

## Key Insights

### 1. You Don't Need Windows

`pyinstxtractor` can extract Windows `.exe` files on Linux. This means:
- You can inspect Windows builds without having Windows
- You can compare Windows vs Linux extracted contents
- You can see exactly which files are missing

### 2. The Diagnostic Runs ON Windows

The GitHub Actions workflow:
- Builds on actual Windows runner
- Tests imports on actual Windows
- Uploads results for you to download

You get **real Windows diagnostic results** without leaving Linux.

### 3. Comparison is Key

```bash
# What's in your working Linux build?
ls build/pyinstaller_diagnostics/DataVisualizer_extracted/

# What's in the broken Windows build?
ls windows-diag/DataVisualizer.exe_extracted/

# What's different?
diff <(ls build/pyinstaller_diagnostics/DataVisualizer_extracted/) \
     <(ls windows-diag/DataVisualizer.exe_extracted/)
```

The diff shows exactly what's missing from Windows.

## Time Comparison

### Yesterday (Guessing):
```
Try fix → Push → Wait 20 min → Fails → Try another → Wait 20 min → ...
Total: Hours of guessing
```

### Today (Diagnostic):
```
Local test (2 min) → Push → GitHub Actions (10 min) → Download artifacts (1 min)
→ See exactly what's wrong → Apply fix → Verify (10 min)
Total: ~25 minutes to identify and fix
```

## Why This Works

1. **Local diagnostic** confirms your Linux build baseline
2. **GitHub Actions** tests real Windows environment
3. **Diagnostic artifacts** capture Windows state
4. **Local analysis** lets you compare on Linux
5. **Targeted fix** based on evidence, not guessing
6. **Verification** confirms fix works

## Tools Summary

| Tool | Purpose | Where It Runs |
|------|---------|---------------|
| `diagnose_pyinstaller.sh` | Baseline your Linux build | Your Linux machine |
| GitHub Actions workflow | Test real Windows build | GitHub's Windows runner |
| `diagnostics-Windows.zip` | Windows diagnostic results | Downloaded to Linux |
| `analyze_existing_build.sh` | Inspect Windows .exe on Linux | Your Linux machine |
| `diff` | Compare builds | Your Linux machine |

## The Workflow in Practice

```bash
# Day 1: Setup and baseline
./tools/diagnose_pyinstaller.sh
git push

# Day 1: Review Windows failure
# Download diagnostics-Windows.zip from GitHub
unzip ~/Downloads/diagnostics-Windows.zip -d ~/windows-diag/
cat ~/windows-diag/diagnostic_runtime.log
# See: ✗ decodelog NOT FOUND

# Day 1: Apply fix
# Edit workflow, add --hidden-import=decodelog
git push

# Day 1: Verify
# Download new diagnostics-Windows.zip
unzip ~/Downloads/diagnostics-Windows.zip -d ~/windows-diag-fixed/
cat ~/windows-diag-fixed/diagnostic_runtime.log
# See: ✓ decodelog found

# Done! Total time: ~1 hour including wait times
```

## Bottom Line

**You debug Windows builds from Linux by:**
1. Running diagnostic on Linux (baseline)
2. Letting GitHub Actions run diagnostic on Windows
3. Downloading Windows results
4. Comparing Windows vs Linux on your machine
5. Applying evidence-based fixes

**No guessing. No Windows VM needed. Just systematic analysis.**
