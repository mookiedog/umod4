#!/bin/bash
# Local PyInstaller diagnostic script
# Run this before pushing to GitHub to catch issues early

# Don't exit on error - we want to generate reports even if builds fail
set +e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
VIZ_DIR="$SCRIPT_DIR/logtools/viz"

echo "========================================"
echo "PyInstaller Diagnostic Tool"
echo "========================================"
echo ""

# Check dependencies
echo "Checking dependencies..."
python3 -m pip show pyinstaller >/dev/null 2>&1 || {
    echo "ERROR: pyinstaller not installed"
    echo "Run: pip install pyinstaller"
    exit 1
}

python3 -m pip show pyinstxtractor-ng >/dev/null 2>&1 || {
    echo "WARNING: pyinstxtractor-ng not installed"
    echo "Installing for extraction analysis..."
    python3 -m pip install pyinstxtractor-ng
}

# Create output directory in build
OUTPUT_DIR="$PROJECT_ROOT/build/pyinstaller_diagnostics"
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

echo "Output directory: $OUTPUT_DIR"
echo ""

# Create diagnostic wrapper if it doesn't exist
if [ ! -f "$VIZ_DIR/diagnostic_wrapper.py" ]; then
    echo "Creating diagnostic wrapper..."
    cat > "$VIZ_DIR/diagnostic_wrapper.py" << 'EOF'
"""Diagnostic wrapper to check module availability at runtime"""
import sys
import os
import importlib.util

def check_bundled_modules():
    """Verify critical modules are accessible"""
    print("=" * 70)
    print("DIAGNOSTIC INFORMATION")
    print("=" * 70)
    print(f"Python version: {sys.version}")
    print(f"Python executable: {sys.executable}")
    print(f"sys.frozen: {getattr(sys, 'frozen', False)}")

    if getattr(sys, 'frozen', False):
        print(f"Running as frozen app")
        if hasattr(sys, '_MEIPASS'):
            print(f"_MEIPASS: {sys._MEIPASS}")
            print(f"Contents of _MEIPASS:")
            try:
                for item in sorted(os.listdir(sys._MEIPASS)):
                    item_path = os.path.join(sys._MEIPASS, item)
                    if os.path.isdir(item_path):
                        print(f"  [DIR]  {item}/")
                    else:
                        size = os.path.getsize(item_path)
                        print(f"  [FILE] {item} ({size} bytes)")
            except Exception as e:
                print(f"  Error listing _MEIPASS: {e}")

    print(f"\nsys.path:")
    for i, path in enumerate(sys.path):
        print(f"  [{i}] {path}")

    print(f"\n__file__: {__file__}")
    print(f"Current working directory: {os.getcwd()}")

    # Critical modules to check
    critical_modules = [
        'decodelog',
        'conversions',
        'stream_config',
        'viz_components',
        'viz_components.config',
        'viz_components.widgets',
        'viz_components.utils',
        'viz_components.rendering',
        'viz_components.data',
        'viz_components.navigation',
    ]

    print("\n" + "=" * 70)
    print("MODULE AVAILABILITY CHECK")
    print("=" * 70)

    missing = []
    for mod in critical_modules:
        try:
            spec = importlib.util.find_spec(mod)
            if spec:
                print(f"✓ {mod:40s} -> {spec.origin}")
            else:
                print(f"✗ {mod:40s} -> NOT FOUND (spec is None)")
                missing.append(mod)
        except (ImportError, ModuleNotFoundError) as e:
            print(f"✗ {mod:40s} -> ERROR: {e}")
            missing.append(mod)

    print("=" * 70)
    if missing:
        print(f"MISSING MODULES: {len(missing)}")
        for mod in missing:
            print(f"  - {mod}")
        print("=" * 70)
        return False
    else:
        print("ALL MODULES FOUND")
        print("=" * 70)
        return True

if __name__ == '__main__':
    success = check_bundled_modules()
    sys.exit(0 if success else 1)
EOF
fi

# Build with PyInstaller
echo "Building with PyInstaller..."
python3 -m PyInstaller --log-level DEBUG \
    --onefile --windowed \
    --add-data "$VIZ_DIR/stream_config.py:." \
    --add-data "$VIZ_DIR/stream_config.yaml:." \
    --add-data "$VIZ_DIR/viz_components:viz_components" \
    --add-data "$SCRIPT_DIR/logtools/decoder/decodelog.py:." \
    --add-data "$SCRIPT_DIR/logtools/decoder/conversions.py:." \
    --name "DataVisualizer" \
    --clean \
    --workpath "$OUTPUT_DIR/build" \
    --distpath "$OUTPUT_DIR/dist" \
    --specpath "$OUTPUT_DIR" \
    "$VIZ_DIR/viz.py" 2>&1 | tee "$OUTPUT_DIR/pyinstaller_build.log"

echo ""
echo "Building diagnostic executable..."
python3 -m PyInstaller --log-level DEBUG \
    --onefile \
    --add-data "$VIZ_DIR/stream_config.py:." \
    --add-data "$VIZ_DIR/stream_config.yaml:." \
    --add-data "$VIZ_DIR/viz_components:viz_components" \
    --add-data "$SCRIPT_DIR/logtools/decoder/decodelog.py:." \
    --add-data "$SCRIPT_DIR/logtools/decoder/conversions.py:." \
    --name "Diagnostics" \
    --clean \
    --workpath "$OUTPUT_DIR/build_diag" \
    --distpath "$OUTPUT_DIR/dist" \
    --specpath "$OUTPUT_DIR" \
    "$VIZ_DIR/diagnostic_wrapper.py" 2>&1 | tee "$OUTPUT_DIR/pyinstaller_diagnostic_build.log"

echo ""
echo "========================================"
echo "ANALYSIS PHASE"
echo "========================================"
echo ""

# Analyze build log
echo "1. Analyzing build log for issues..."
echo "------------------------------------"
echo ""
echo "WARNINGS:"
grep -i "warning" "$OUTPUT_DIR/pyinstaller_build.log" || echo "  No warnings found"
echo ""
echo "ERRORS:"
grep -i "error" "$OUTPUT_DIR/pyinstaller_build.log" || echo "  No errors found"
echo ""
echo "NOT FOUND:"
grep -i "not found" "$OUTPUT_DIR/pyinstaller_build.log" || echo "  No 'not found' messages"
echo ""
echo "MISSING:"
grep -i "missing" "$OUTPUT_DIR/pyinstaller_build.log" || echo "  No 'missing' messages"
echo ""

# Show spec file
echo "2. Generated spec file:"
echo "-----------------------"
cat "$OUTPUT_DIR/DataVisualizer.spec"
echo ""

# Run diagnostic
echo "3. Running diagnostic executable..."
echo "-----------------------------------"
"$OUTPUT_DIR/dist/Diagnostics" 2>&1 | tee "$OUTPUT_DIR/diagnostic_runtime.log"
echo ""

# Extract and analyze
echo "4. Extracting executable contents..."
echo "------------------------------------"
python3 -m pyinstxtractor "$OUTPUT_DIR/dist/DataVisualizer"

echo ""
echo "Contents of extracted archive:"
find "$OUTPUT_DIR/DataVisualizer_extracted" -type f -o -type d | sort | while read path; do
    if [ -d "$path" ]; then
        echo "[DIR]  $path/"
    else
        size=$(stat -c%s "$path" 2>/dev/null || stat -f%z "$path" 2>/dev/null)
        echo "[FILE] $path ($size bytes)"
    fi
done | tee "$OUTPUT_DIR/extracted_contents.txt"

echo ""
echo "5. Searching for our modules in extracted archive..."
echo "-----------------------------------------------------"
echo "decodelog:"
find "$OUTPUT_DIR/DataVisualizer_extracted" -name "*decodelog*" || echo "  NOT FOUND"
echo ""
echo "conversions:"
find "$OUTPUT_DIR/DataVisualizer_extracted" -name "*conversions*" || echo "  NOT FOUND"
echo ""
echo "viz_components:"
find "$OUTPUT_DIR/DataVisualizer_extracted" -name "*viz_components*" || echo "  NOT FOUND"
echo ""
echo "stream_config:"
find "$OUTPUT_DIR/DataVisualizer_extracted" -name "*stream_config*" || echo "  NOT FOUND"
echo ""

# Generate report
echo "6. Generating diagnostic report..."
echo "-----------------------------------"
cat > "$OUTPUT_DIR/DIAGNOSTIC_REPORT.md" << 'ENDREPORT'
# PyInstaller Diagnostic Report - Local Build

## Build Information
- OS: $(uname -s)
- Python Version: $(python3 --version)
- PyInstaller Version: $(python3 -m pip show pyinstaller | grep Version | cut -d' ' -f2)
- Build Date: $(date)
- Working Directory: $OUTPUT_DIR

## Files Generated
- PyInstaller build log: pyinstaller_build.log
- PyInstaller diagnostic build log: pyinstaller_diagnostic_build.log
- Spec file: DataVisualizer.spec
- Diagnostic spec file: Diagnostics.spec
- Runtime diagnostic log: diagnostic_runtime.log
- Extracted contents listing: extracted_contents.txt
- Extracted archive: DataVisualizer_extracted/

## Module Availability Check

### Runtime Results
\`\`\`
$(grep -A 30 "MODULE AVAILABILITY CHECK" "$OUTPUT_DIR/diagnostic_runtime.log" || echo "Module check not found in log")
\`\`\`

## Quick Checklist

Run these checks manually:

- [ ] Is decodelog.pyc present in extracted archive?
- [ ] Is conversions.pyc present in extracted archive?
- [ ] Is viz_components/ directory present with all submodules?
- [ ] Is stream_config.py present?
- [ ] Is stream_config.yaml present?

## Extracted Files

See extracted_contents.txt for full listing.

Key files found:
\`\`\`
$(grep -E "(decodelog|conversions|viz_components|stream_config)" "$OUTPUT_DIR/extracted_contents.txt" || echo "No key files found!")
\`\`\`

## Build Warnings/Errors

### Warnings
\`\`\`
$(grep -i "warning" "$OUTPUT_DIR/pyinstaller_build.log" | head -20 || echo "None")
\`\`\`

### Errors
\`\`\`
$(grep -i "error" "$OUTPUT_DIR/pyinstaller_build.log" | head -20 || echo "None")
\`\`\`

### Not Found
\`\`\`
$(grep -i "not found" "$OUTPUT_DIR/pyinstaller_build.log" | head -20 || echo "None")
\`\`\`

## Next Steps

If modules are missing:

1. Check spec file Analysis() section for pathex and hiddenimports
2. Verify --add-data paths are correct
3. Consider using --paths to add tools/logtools to module search path
4. Add explicit --hidden-import for local modules
5. Try creating a proper Python package structure with __init__.py files

## Spec File

\`\`\`python
$(cat "$OUTPUT_DIR/DataVisualizer.spec" 2>/dev/null || echo "Spec file not found")
\`\`\`

ENDREPORT

echo ""
echo "========================================"
echo "DIAGNOSTIC COMPLETE"
echo "========================================"
echo ""
echo "Results saved to: $OUTPUT_DIR"
echo ""
echo "Key files:"
echo "  - DIAGNOSTIC_REPORT.md      : Summary report"
echo "  - diagnostic_runtime.log    : Module availability at runtime"
echo "  - extracted_contents.txt    : Full list of bundled files"
echo "  - DataVisualizer_extracted/ : Extracted executable contents"
echo ""
echo "View the report:"
echo "  cat $OUTPUT_DIR/DIAGNOSTIC_REPORT.md"
echo ""
