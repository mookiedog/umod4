# PyInstaller Diagnostics - Documentation Index

## Start Here

**New to the diagnostic system?** → [GETTING_STARTED.md](GETTING_STARTED.md)

**Need a quick command?** → [QUICK_REFERENCE.md](QUICK_REFERENCE.md)

---

## Documentation by Purpose

### For Quick Answers
- **[GETTING_STARTED.md](GETTING_STARTED.md)** - Simple step-by-step workflow
- **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** - Command cheat sheet and common fixes

### For Understanding the System
- **[DIAGNOSTIC_SYSTEM_README.md](DIAGNOSTIC_SYSTEM_README.md)** - What it solves and how it works
- **[DIAGNOSTIC_TOOLS_SUMMARY.md](DIAGNOSTIC_TOOLS_SUMMARY.md)** - Complete tool overview
- **[DIAGNOSTIC_ARCHITECTURE.md](DIAGNOSTIC_ARCHITECTURE.md)** - Visual diagrams and data flow

### For Debugging Issues
- **[PYINSTALLER_DIAGNOSTICS.md](PYINSTALLER_DIAGNOSTICS.md)** - Detailed debugging guide with examples

### For Runtime Integration
- **[ENABLE_DIAGNOSTICS.md](logtools/viz/ENABLE_DIAGNOSTICS.md)** - Add self-checks to the app

---

## Tools Reference

### Scripts
| Script | Purpose | Runtime |
|--------|---------|---------|
| `diagnose_pyinstaller.sh` | Full local diagnostic | ~1-2 min |
| `analyze_existing_build.sh` | Inspect existing .exe | ~30 sec |

### Modules
| Module | Purpose |
|--------|---------|
| `logtools/viz/self_diagnostic.py` | Runtime module checker |

### Workflows
| Workflow | Purpose |
|----------|---------|
| `.github/workflows/pyinstaller-diagnostics.yml` | Cross-platform CI |

---

## Quick Navigation

### I want to...

**Test my build before pushing**
→ Run `./tools/diagnose_pyinstaller.sh`
→ See [GETTING_STARTED.md](GETTING_STARTED.md)

**Understand what went wrong**
→ Check `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md`
→ See [PYINSTALLER_DIAGNOSTICS.md](PYINSTALLER_DIAGNOSTICS.md)

**Fix a missing module**
→ See [QUICK_REFERENCE.md](QUICK_REFERENCE.md) → "Common Fixes"

**Test on all platforms**
→ Push to GitHub or manually trigger workflow
→ See [GETTING_STARTED.md](GETTING_STARTED.md) → "Step 7"

**Analyze a downloaded .exe**
→ Run `./tools/analyze_existing_build.sh path/to/exe`

**Add runtime checks to my app**
→ See [ENABLE_DIAGNOSTICS.md](logtools/viz/ENABLE_DIAGNOSTICS.md)

**Understand the architecture**
→ See [DIAGNOSTIC_ARCHITECTURE.md](DIAGNOSTIC_ARCHITECTURE.md)

---

## Reading Order Recommendations

### For First-Time Users
1. [GETTING_STARTED.md](GETTING_STARTED.md)
2. [QUICK_REFERENCE.md](QUICK_REFERENCE.md)
3. Run `./tools/diagnose_pyinstaller.sh`
4. Review the generated report

### For Developers Fixing Issues
1. Run `./tools/diagnose_pyinstaller.sh`
2. Review `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md`
3. Check [QUICK_REFERENCE.md](QUICK_REFERENCE.md) for fixes
4. If stuck, consult [PYINSTALLER_DIAGNOSTICS.md](PYINSTALLER_DIAGNOSTICS.md)

### For Understanding the System
1. [DIAGNOSTIC_SYSTEM_README.md](DIAGNOSTIC_SYSTEM_README.md)
2. [DIAGNOSTIC_ARCHITECTURE.md](DIAGNOSTIC_ARCHITECTURE.md)
3. [DIAGNOSTIC_TOOLS_SUMMARY.md](DIAGNOSTIC_TOOLS_SUMMARY.md)

---

## File Locations

```
tools/
├── diagnose_pyinstaller.sh              ← Main local diagnostic
├── analyze_existing_build.sh            ← Analyze existing builds
│
├── DIAGNOSTICS_INDEX.md                 ← This file (navigation)
├── GETTING_STARTED.md                   ← Quick start guide
├── QUICK_REFERENCE.md                   ← Command cheat sheet
│
├── DIAGNOSTIC_SYSTEM_README.md          ← System overview
├── DIAGNOSTIC_TOOLS_SUMMARY.md          ← Complete tool guide
├── DIAGNOSTIC_ARCHITECTURE.md           ← Visual architecture
├── PYINSTALLER_DIAGNOSTICS.md           ← Detailed debugging
│
└── logtools/viz/
    ├── self_diagnostic.py               ← Runtime checker
    └── ENABLE_DIAGNOSTICS.md            ← Integration guide

.github/workflows/
└── pyinstaller-diagnostics.yml          ← CI workflow
```

---

## Essential Commands

```bash
# Setup (one-time)
python3 -m pip install pyinstxtractor-ng

# Run diagnostics
./tools/diagnose_pyinstaller.sh

# View results
cat build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md

# Analyze existing build
./tools/analyze_existing_build.sh path/to/executable
```

---

## Common Questions

**Q: Which tool should I run?**
A: Start with `./tools/diagnose_pyinstaller.sh` - it does everything.

**Q: Do I need to run manually or is it automatic?**
A: Both! Run locally before pushing. GitHub Actions runs automatically after push.

**Q: How do I know if my build is good?**
A: All modules show ✓ in `diagnostic_runtime.log`

**Q: What if modules are missing?**
A: Check [QUICK_REFERENCE.md](QUICK_REFERENCE.md) → "Common Fixes" section

**Q: How long does it take?**
A: Local: 1-2 min, GitHub Actions: 5-10 min (3 platforms in parallel)

**Q: Can I skip the diagnostic?**
A: You can, but it's much faster to catch issues locally than wait for CI failures.

---

## Documentation Maintenance

When adding new modules to the visualizer:
1. Update `critical_modules` in `diagnostic_wrapper.py` (used by workflows)
2. Update `required_modules` in `self_diagnostic.py`
3. Test that diagnostics catch the new dependency

When changing PyInstaller options:
1. Update both the main build workflow AND diagnostic workflow
2. Run local diagnostic to verify
3. Check GitHub Actions artifacts to confirm all platforms work

---

## Support

If you're stuck:
1. Run `./tools/diagnose_pyinstaller.sh`
2. Gather these files:
   - `build/pyinstaller_diagnostics/DIAGNOSTIC_REPORT.md`
   - `build/pyinstaller_diagnostics/diagnostic_runtime.log`
   - `build/pyinstaller_diagnostics/extracted_contents.txt`
3. Include them when asking for help

The diagnostic output contains 90% of what's needed to diagnose the issue.
