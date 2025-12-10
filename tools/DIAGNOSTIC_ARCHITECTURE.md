# Diagnostic System Architecture

## Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    PyInstaller Diagnostic System                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────────┐  │
│  │ Local Scripts  │  │ GitHub Actions │  │ Runtime Module   │  │
│  │                │  │                │  │                  │  │
│  │ • diagnose_    │  │ • Multi-       │  │ • self_          │  │
│  │   pyinstaller  │  │   platform     │  │   diagnostic.py  │  │
│  │   .sh          │  │   build        │  │                  │  │
│  │                │  │                │  │ • Import checks  │  │
│  │ • analyze_     │  │ • Extract      │  │                  │  │
│  │   existing_    │  │   contents     │  │ • Embedded in    │  │
│  │   build.sh     │  │                │  │   frozen app     │  │
│  │                │  │ • Compare      │  │                  │  │
│  │ • Fast local   │  │   platforms    │  │ • Startup        │  │
│  │   iteration    │  │                │  │   validation     │  │
│  └────────────────┘  └────────────────┘  └──────────────────┘  │
│           │                   │                     │            │
│           └───────────────────┴─────────────────────┘            │
│                               │                                  │
│                               ▼                                  │
│                    ┌──────────────────────┐                      │
│                    │  Shared Components   │                      │
│                    │                      │                      │
│                    │  • pyinstxtractor    │                      │
│                    │  • importlib checks  │                      │
│                    │  • Log analysis      │                      │
│                    │  • Report generation │                      │
│                    └──────────────────────┘                      │
└─────────────────────────────────────────────────────────────────┘
```

## Data Flow: Local Diagnostic Run

```
User runs:
./tools/diagnose_pyinstaller.sh
         │
         ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. BUILD PHASE                                               │
│                                                              │
│  PyInstaller --log-level DEBUG                              │
│      ├─> DataVisualizer (main app)                          │
│      └─> Diagnostics (test executable)                      │
│                                                              │
│  Captures:                                                   │
│  • pyinstaller_build.log (full build output)                │
│  • DataVisualizer.spec (build configuration)                │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. EXTRACTION PHASE                                          │
│                                                              │
│  pyinstxtractor DataVisualizer                              │
│      └─> DataVisualizer_extracted/                          │
│          ├─> PYZ-00.pyz (Python modules archive)            │
│          ├─> *.pyc (compiled Python files)                  │
│          ├─> *.so / *.dll (native libraries)                │
│          └─> data files                                     │
│                                                              │
│  Generates:                                                  │
│  • extracted_contents.txt (file listing)                    │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. RUNTIME TEST PHASE                                        │
│                                                              │
│  ./dist/Diagnostics                                          │
│      ├─> Checks sys.frozen, sys._MEIPASS                    │
│      ├─> Lists bundle contents                              │
│      ├─> Tests importlib.util.find_spec() for each module   │
│      └─> Reports ✓ or ✗ for each required module           │
│                                                              │
│  Generates:                                                  │
│  • diagnostic_runtime.log (import test results)             │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. ANALYSIS PHASE                                            │
│                                                              │
│  Script analyzes:                                            │
│  ├─> Build log (warnings, errors, "not found")              │
│  ├─> Spec file (datas, hiddenimports, pathex)               │
│  ├─> Extracted contents (presence of .pyc files)            │
│  └─> Runtime log (import success/failure)                   │
│                                                              │
│  Cross-references:                                           │
│  • Is module missing from runtime? → Check extracted         │
│  • Is file in extracted? → Check build log warnings         │
│  • Missing from build? → Recommend --paths or --hidden      │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. REPORT GENERATION                                         │
│                                                              │
│  DIAGNOSTIC_REPORT.md:                                       │
│  ├─> Build summary                                           │
│  ├─> Module availability table                              │
│  ├─> Missing files list                                     │
│  ├─> Build warnings/errors                                  │
│  ├─> Recommended fixes                                       │
│  └─> Next steps                                             │
└─────────────────────────────────────────────────────────────┘
         │
         ▼
    User reviews report
    Applies targeted fix
    Re-runs diagnostic
```

## GitHub Actions Workflow Architecture

```
GitHub Actions Trigger
(push to main or manual)
         │
         ├──────────────┬──────────────┬──────────────┐
         ▼              ▼              ▼              │
   ┌─────────┐    ┌─────────┐    ┌─────────┐        │
   │ Windows │    │  Linux  │    │  macOS  │        │
   │  Job    │    │   Job   │    │   Job   │        │
   └────┬────┘    └────┬────┘    └────┬────┘        │
        │              │              │              │
        │  (All run in parallel)      │              │
        │              │              │              │
        ▼              ▼              ▼              │
   For each platform:                                │
   ┌──────────────────────────────────────┐          │
   │ 1. Setup Python & deps               │          │
   │ 2. Install pyinstxtractor-ng         │          │
   │ 3. Create diagnostic_wrapper.py      │          │
   │ 4. Build with PyInstaller (DEBUG)    │          │
   │ 5. Build diagnostic executable       │          │
   │ 6. Analyze logs for warnings         │          │
   │ 7. Extract spec file                 │          │
   │ 8. Run diagnostic executable         │          │
   │ 9. Extract main executable           │          │
   │ 10. Compare with source files        │          │
   │ 11. Generate platform report         │          │
   │ 12. Upload artifact bundle           │          │
   └──────────────┬───────────────────────┘          │
                  │                                   │
                  └───────────┬───────────────────────┘
                              ▼
                   ┌───────────────────────┐
                   │   Compare Job         │
                   │   (runs after all)    │
                   │                       │
                   │ 1. Download all       │
                   │    artifacts          │
                   │                       │
                   │ 2. Compare module     │
                   │    availability       │
                   │    across platforms   │
                   │                       │
                   │ 3. Generate           │
                   │    comparison_        │
                   │    report.md          │
                   │                       │
                   │ 4. Upload report      │
                   └───────────────────────┘
                              │
                              ▼
                   User downloads artifacts:
                   • diagnostics-Windows
                   • diagnostics-Linux
                   • diagnostics-macOS
                   • comparison-report
```

## Runtime Self-Diagnostic Flow

```
Application Startup
         │
         ▼
   Import self_diagnostic
         │
         ▼
   Run quick_check()
         │
         ├─> Check sys.frozen
         │   (Are we running as frozen app?)
         │
         ├─> For each required module:
         │   ├─> importlib.util.find_spec(module)
         │   ├─> Record found ✓ or missing ✗
         │   └─> Store location if found
         │
         ▼
   All modules found?
         │
         ├─ YES ──> Continue normal startup
         │
         └─ NO ───> Generate diagnostic report
                    Print to console/log
                    Optionally exit with error
```

## Module Dependency Graph

```
┌───────────────────────────────────────────────────────────┐
│ PyInstaller Executable                                     │
├───────────────────────────────────────────────────────────┤
│                                                            │
│  viz.py (main)                                             │
│    │                                                       │
│    ├─> PyQt6, pyqtgraph, numpy, pandas, h5py (3rd party) │
│    │                                                       │
│    ├─> stream_config.py (local)                           │
│    │   └─> stream_config.yaml (data)                      │
│    │                                                       │
│    ├─> decodelog.py (local)                               │
│    │   └─> conversions.py (local)                         │
│    │                                                       │
│    └─> viz_components/ (local package)                    │
│        ├─> __init__.py                                    │
│        ├─> config.py                                      │
│        ├─> widgets.py                                     │
│        ├─> utils.py                                       │
│        ├─> rendering.py                                   │
│        ├─> data.py                                        │
│        └─> navigation.py                                  │
│                                                            │
│  self_diagnostic.py (optional)                             │
│    └─> Verifies all above are importable                  │
│                                                            │
└───────────────────────────────────────────────────────────┘
```

## Diagnostic Decision Tree

```
                    Run Diagnostic
                          │
                          ▼
              Check diagnostic_runtime.log
                          │
        ┌─────────────────┴─────────────────┐
        │                                   │
        ▼                                   ▼
   All modules ✓                      Some modules ✗
        │                                   │
        ▼                                   ▼
   SUCCESS!                      Check extracted_contents.txt
   No action needed                        │
                              ┌─────────────┴─────────────┐
                              │                           │
                              ▼                           ▼
                      Module .pyc present        Module .pyc missing
                              │                           │
                              ▼                           ▼
                   Module bundled as data      Check pyinstaller_build.log
                   (not as code)                         │
                              │                ┌─────────┴─────────┐
                              │                │                   │
                              │                ▼                   ▼
                              │        "not found"        No warnings
                              │         warning           about module
                              │                │                   │
                              │                ▼                   ▼
                              │         Module not          Check if module
                              │         in search           is Python package
                              │         path                with __init__.py
                              │                │                   │
                              ▼                ▼                   ▼
                     Add to              Add to              Package not
                  --hidden-import      --paths              traversed by
                                                            PyInstaller
                                                                 │
                                                                 ▼
                                                            Add each
                                                            submodule to
                                                          --hidden-import
```

## File Output Structure

```
After running diagnose_pyinstaller.sh:

build/pyinstaller_diagnostics/
├── DIAGNOSTIC_REPORT.md          ← Start here (main report)
│
├── Build artifacts:
│   ├── pyinstaller_build.log     ← Full PyInstaller output
│   ├── pyinstaller_diagnostic_build.log
│   ├── DataVisualizer.spec       ← Build configuration
│   └── Diagnostics.spec
│
├── Runtime test:
│   └── diagnostic_runtime.log    ← Module import results
│
├── Extracted contents:
│   ├── extracted_contents.txt    ← File listing
│   ├── python_modules.txt        ← All .pyc files (optional)
│   └── data_files.txt            ← All data files (optional)
│
├── Extracted executables:
│   ├── DataVisualizer_extracted/
│   │   ├── PYZ-00.pyz
│   │   ├── base_library.zip
│   │   ├── decodelog.pyc         ← Should be here
│   │   ├── conversions.pyc       ← Should be here
│   │   ├── stream_config.py      ← Should be here
│   │   ├── stream_config.yaml    ← Should be here
│   │   ├── viz_components/       ← Should be here
│   │   │   ├── __init__.pyc
│   │   │   ├── config.pyc
│   │   │   └── ...
│   │   └── [many other files]
│   │
│   └── Diagnostics_extracted/
│       └── [similar structure]
│
└── Build artifacts:
    ├── build/                    ← Intermediate build files
    ├── build_diag/
    └── dist/
        ├── DataVisualizer        ← Final executable
        └── Diagnostics           ← Test executable
```

## Information Flow Diagram

```
┌────────────────┐
│ Source Code    │
│ • viz.py       │
│ • decodelog.py │
│ • etc.         │
└───────┬────────┘
        │
        ▼
┌────────────────┐     Build Log      ┌──────────────┐
│  PyInstaller   │─────────────────────>│  Analysis    │
└───────┬────────┘    (warnings,       │  Engine      │
        │              errors)          └──────┬───────┘
        │                                      │
        ▼                                      │
┌────────────────┐                             │
│  Executable    │                             │
└───────┬────────┘                             │
        │                                      │
        ▼                                      │
┌────────────────┐     Extraction     ┌────────▼───────┐
│ pyinstxtractor │─────────────────────>│  Analysis      │
└────────────────┘    (file list)     │  Engine        │
        │                              └────────┬───────┘
        ▼                                       │
┌────────────────┐                              │
│  Extracted     │                              │
│  Contents      │──────────────────────────────┘
└───────┬────────┘    (actual files)
        │
        ▼
┌────────────────┐     Import Test    ┌──────────────┐
│  Runtime Test  │─────────────────────>│  Analysis    │
│  (Diagnostics) │    (✓ or ✗)        │  Engine      │
└────────────────┘                     └──────┬───────┘
                                              │
                                              ▼
                                       ┌──────────────┐
                                       │ DIAGNOSTIC   │
                                       │ REPORT       │
                                       │              │
                                       │ • Missing    │
                                       │   modules    │
                                       │ • Root cause │
                                       │ • Fix steps  │
                                       └──────────────┘
```

## Key Insight: Closing the Feedback Loop

**Before (yesterday):**
```
Try fix → Push → Wait 20 min → Build fails → ??? → Try another fix → ...
         └─ No visibility into what's actually wrong
```

**After (today):**
```
Run local diagnostic (2 min) → See exactly what's missing → Apply targeted fix → Verify → Done
                             └─ Complete visibility: build log, extracted files, runtime test
```

## Summary

The diagnostic system consists of three complementary approaches:

1. **Local Scripts** - Fast iteration during development
2. **GitHub Actions** - Automated cross-platform verification
3. **Runtime Module** - Self-checking in deployed apps

All tools share common techniques:
- Extract executables with pyinstxtractor
- Test imports with importlib
- Analyze logs systematically
- Generate actionable reports

The result: **Evidence-based debugging instead of blind guessing.**
