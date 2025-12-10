# Optional: Enable Startup Diagnostics in viz.py

If you want the visualizer to self-diagnose on startup (useful during development or troubleshooting packaging issues), add this code to the beginning of `viz.py`.

## Quick Integration (Minimal)

Add at the very top of `viz.py`, right after the docstring:

```python
# Optional: Self-diagnostic on startup (can be removed for production)
if __name__ == '__main__':
    import self_diagnostic
    if not self_diagnostic.quick_check():
        print("\nWARNING: Self-diagnostic found issues")
        print("The application may not work correctly")
        import time
        time.sleep(3)  # Give user time to see warning
```

This will silently check modules and show a brief warning if something is wrong.

## Full Integration (Verbose)

For detailed diagnostic output, use this instead:

```python
# Optional: Self-diagnostic on startup (can be removed for production)
if __name__ == '__main__':
    import sys
    import self_diagnostic

    # Run diagnostic with full output
    if '--diagnostic' in sys.argv or '--self-test' in sys.argv:
        # User explicitly requested diagnostic
        self_diagnostic.check_and_report(exit_on_failure=True, verbose=True)
        sys.exit(0)  # Exit after showing report

    # Quick check on every startup
    result = self_diagnostic.run_diagnostic(verbose=False)
    if not result.success:
        print(result.generate_report())
        print("\nThe visualizer cannot start due to missing dependencies.")
        print("If you downloaded a release, this is a packaging bug - please report it.")
        print("If running from source, ensure all dependencies are installed.")
        sys.exit(1)
```

This version:
- Supports `--diagnostic` flag for explicit self-test
- Shows full report if modules are missing
- Exits immediately if critical modules unavailable (prevents confusing errors later)

## PyInstaller Integration

When building with PyInstaller, add the diagnostic module:

```bash
python -m PyInstaller \
  --add-data "tools/logtools/viz/self_diagnostic.py:." \
  # ... other options
  tools/logtools/viz/viz.py
```

## Testing

Test that diagnostics work:

```bash
# Run explicit diagnostic
python tools/logtools/viz/viz.py --diagnostic

# Run standalone diagnostic module
python tools/logtools/viz/self_diagnostic.py --verbose

# Test in frozen app (after building with PyInstaller)
./dist/DataVisualizer --diagnostic
```

## Removing for Production

The diagnostic code is entirely optional. To remove:

1. Delete or comment out the diagnostic import/check in `viz.py`
2. Remove `self_diagnostic.py` from `--add-data` in PyInstaller command
3. Delete `self_diagnostic.py` if desired

The main application has no dependency on the diagnostic module - it's purely an optional startup check.
