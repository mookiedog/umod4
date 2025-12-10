# PyInstaller Diagnostics - Quick Reference Card

## 🚀 Quick Commands

```bash
# Test locally before pushing
./tools/diagnose_pyinstaller.sh

# Analyze an existing build
./tools/analyze_existing_build.sh path/to/DataVisualizer.exe

# Run self-diagnostic standalone
python tools/logtools/viz/self_diagnostic.py --verbose
```

## 📋 Checklist: Is My Build Good?

- [ ] `diagnostic_runtime.log` shows all modules with ✓
- [ ] `extracted_contents.txt` contains decodelog.pyc
- [ ] `extracted_contents.txt` contains conversions.pyc
- [ ] `extracted_contents.txt` contains viz_components/ directory
- [ ] `extracted_contents.txt` contains stream_config.py
- [ ] `pyinstaller_build.log` has no "warning" about custom modules
- [ ] Application launches without ImportError

## 🔍 Where to Look When Debugging

| Problem | Check This File | Look For |
|---------|----------------|----------|
| Module not importable | `diagnostic_runtime.log` | Lines with ✗ symbol |
| File not bundled | `extracted_contents.txt` | Search for filename |
| Build issues | `pyinstaller_build.log` | "warning", "not found", "error" |
| Platform differences | `comparison_report.md` | Module availability sections |

## 🛠️ Common Fixes

### Module not found during build
```bash
--paths=tools/logtools \
--paths=tools/logtools/decoder \
--paths=tools/logtools/viz
```

### Module bundled but not importable
```bash
--hidden-import=decodelog \
--hidden-import=conversions \
--hidden-import=viz_components
```

### Submodules missing
```bash
--hidden-import=viz_components.config \
--hidden-import=viz_components.widgets \
--hidden-import=viz_components.utils \
--hidden-import=viz_components.rendering \
--hidden-import=viz_components.data \
--hidden-import=viz_components.navigation
```

### Data files not included
```bash
--add-data "source/path:destination"
# Linux/Mac uses :
# Windows in GitHub Actions also uses : (not ;)
```

## 📁 Key Files After Running Diagnostics

```
build/pyinstaller_diagnostics/
├── DIAGNOSTIC_REPORT.md          ← Start here
├── diagnostic_runtime.log         ← Module import test
├── extracted_contents.txt         ← What was bundled
├── pyinstaller_build.log          ← Build warnings/errors
└── DataVisualizer_extracted/      ← Actual files
```

## 🔄 The Debug Loop

```
1. Run diagnostic
   ↓
2. Check runtime log (modules found?)
   ↓
3. Check extracted contents (files present?)
   ↓
4. Check build log (warnings?)
   ↓
5. Apply fix
   ↓
6. Re-run diagnostic
   ↓
7. Verify all ✓
```

## 🌐 GitHub Actions

**Trigger workflow:**
- Automatically: Push to main with changes in `tools/logtools/`
- Manually: Actions → PyInstaller Diagnostics → Run workflow

**Download artifacts:**
- Actions → Workflow run → Artifacts section
- Download: diagnostics-Windows, diagnostics-Linux, diagnostics-macOS
- Compare: comparison-report

**What to check:**
1. Do all platforms show the same modules?
2. Does Windows have ✗ where Linux has ✓?
3. What's different in Windows extracted_contents.txt?

## 🐛 Symptoms → Solutions

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| ImportError at startup | Module not bundled | Add to `--hidden-import` |
| Module .pyc missing | Not in search path | Add to `--paths` |
| Windows fails, Linux works | Path separator issue | Check `--add-data` uses `:` |
| Submodules missing | Package not traversed | Add each submodule to `--hidden-import` |
| File exists but not importable | Bundled as data not code | Move to `--hidden-import` |

## 📖 Full Documentation

- `DIAGNOSTIC_TOOLS_SUMMARY.md` - Complete system overview
- `PYINSTALLER_DIAGNOSTICS.md` - Detailed debugging guide
- `ENABLE_DIAGNOSTICS.md` - Add runtime checks to app

## ⚡ Emergency: Build Is Broken!

```bash
# 1. Get evidence
./tools/diagnose_pyinstaller.sh

# 2. Find the missing module
grep "✗" build/pyinstaller_diagnostics/diagnostic_runtime.log

# 3. Confirm it's not bundled
grep -i "MODULE_NAME" build/pyinstaller_diagnostics/extracted_contents.txt

# 4. Check build warnings
grep -i "MODULE_NAME" build/pyinstaller_diagnostics/pyinstaller_build.log

# 5. Apply fix and repeat
```

## 💡 Pro Tips

- Run diagnostics **before** pushing to GitHub
- Keep diagnostic output when reporting bugs
- Compare working (Linux) vs broken (Windows) extracted contents
- Use `--log-level DEBUG` for maximum information
- Test the diagnostic executable, not just main app
- Platform differences? Check path separators and case sensitivity

## 🎯 Success Criteria

All green:
- ✅ Local diagnostic passes
- ✅ All platforms show identical module availability
- ✅ No warnings in any build log
- ✅ Application launches and runs correctly
- ✅ No ImportError or ModuleNotFoundError

## 📞 Getting Help

Include these files:
1. `DIAGNOSTIC_REPORT.md`
2. `diagnostic_runtime.log`
3. `extracted_contents.txt`
4. Relevant warnings from `pyinstaller_build.log`

From GitHub Actions:
1. Download all platform diagnostic artifacts
2. Include `comparison_report.md`
3. Note which platform works vs fails
