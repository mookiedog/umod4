#!/bin/bash
# Analyze an existing PyInstaller executable
# Usage: ./analyze_existing_build.sh path/to/DataVisualizer[.exe]

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <path-to-executable>"
    echo ""
    echo "Examples:"
    echo "  $0 dist/DataVisualizer"
    echo "  $0 ~/Downloads/DataVisualizer.exe"
    echo "  $0 artifacts/DataVisualizer-Windows/DataVisualizer.exe"
    exit 1
fi

EXECUTABLE="$1"

if [ ! -f "$EXECUTABLE" ]; then
    echo "ERROR: File not found: $EXECUTABLE"
    exit 1
fi

# Get absolute path
EXECUTABLE=$(realpath "$EXECUTABLE")
EXEC_NAME=$(basename "$EXECUTABLE")
EXEC_DIR=$(dirname "$EXECUTABLE")

echo "========================================"
echo "PyInstaller Executable Analyzer"
echo "========================================"
echo "Analyzing: $EXECUTABLE"
echo "Size: $(ls -lh "$EXECUTABLE" | awk '{print $5}')"
echo ""

# Create analysis directory
ANALYSIS_DIR="$EXEC_DIR/${EXEC_NAME}_analysis"
mkdir -p "$ANALYSIS_DIR"
cd "$ANALYSIS_DIR"

echo "Output directory: $ANALYSIS_DIR"
echo ""

# Check for extraction tool
python3 -m pip show pyinstxtractor-ng >/dev/null 2>&1 || {
    echo "Installing pyinstxtractor-ng..."
    python3 -m pip install pyinstxtractor-ng
}

# Extract
echo "Extracting executable..."
python3 -m pyinstxtractor_ng "$EXECUTABLE"

# Find extracted directory
EXTRACTED_DIR=$(find . -maxdepth 1 -name "${EXEC_NAME}_extracted" -type d | head -1)

if [ -z "$EXTRACTED_DIR" ]; then
    EXTRACTED_DIR=$(find . -maxdepth 1 -name "*_extracted" -type d | head -1)
fi

if [ -z "$EXTRACTED_DIR" ]; then
    echo "ERROR: Could not find extracted directory"
    exit 1
fi

echo "Extracted to: $EXTRACTED_DIR"
echo ""

# Generate file listing
echo "Generating file listing..."
find "$EXTRACTED_DIR" -type f -o -type d | sort > file_listing.txt

# Count files
TOTAL_FILES=$(find "$EXTRACTED_DIR" -type f | wc -l)
TOTAL_DIRS=$(find "$EXTRACTED_DIR" -type d | wc -l)
echo "Total files: $TOTAL_FILES"
echo "Total directories: $TOTAL_DIRS"
echo ""

# Search for key modules
echo "Searching for required modules..."
echo "================================="
echo ""

declare -a REQUIRED_MODULES=(
    "decodelog"
    "conversions"
    "stream_config"
    "viz_components"
)

FOUND_COUNT=0
MISSING_COUNT=0

for mod in "${REQUIRED_MODULES[@]}"; do
    echo "Checking: $mod"
    RESULTS=$(find "$EXTRACTED_DIR" -name "*${mod}*" 2>/dev/null)
    if [ -n "$RESULTS" ]; then
        echo "  ✓ FOUND:"
        echo "$RESULTS" | while read file; do
            if [ -f "$file" ]; then
                SIZE=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null)
                echo "    - $(basename "$file") ($SIZE bytes)"
            elif [ -d "$file" ]; then
                SUBFILES=$(find "$file" -name "*.pyc" | wc -l)
                echo "    - $(basename "$file")/ ($SUBFILES .pyc files)"
            fi
        done
        ((FOUND_COUNT++))
    else
        echo "  ✗ NOT FOUND"
        ((MISSING_COUNT++))
    fi
    echo ""
done

# Check for common Python modules
echo "Checking standard dependencies..."
echo "================================="
echo ""

declare -a STD_MODULES=(
    "PyQt6"
    "numpy"
    "pandas"
    "h5py"
    "pyqtgraph"
)

for mod in "${STD_MODULES[@]}"; do
    if find "$EXTRACTED_DIR" -name "*${mod}*" 2>/dev/null | grep -q .; then
        echo "  ✓ $mod"
    else
        echo "  ✗ $mod (not found)"
    fi
done
echo ""

# List Python modules (.pyc files)
echo "Listing all Python modules..."
echo "============================="
find "$EXTRACTED_DIR" -name "*.pyc" | sort > python_modules.txt
MODULE_COUNT=$(wc -l < python_modules.txt)
echo "Found $MODULE_COUNT Python modules (.pyc files)"
echo "Full list saved to: python_modules.txt"
echo ""

# Check for our modules specifically
echo "Custom module check..."
echo "====================="
grep -E "(decodelog|conversions|viz_components|stream_config)" python_modules.txt || echo "No custom modules found in .pyc list!"
echo ""

# List data files
echo "Listing data files..."
echo "===================="
find "$EXTRACTED_DIR" -type f ! -name "*.pyc" ! -name "*.so" ! -name "*.dll" ! -name "*.dylib" ! -name "*.exe" | sort > data_files.txt
DATA_COUNT=$(wc -l < data_files.txt)
echo "Found $DATA_COUNT data files"
echo "Full list saved to: data_files.txt"
echo ""

# Check for YAML config
if grep -q "stream_config.yaml" data_files.txt; then
    echo "  ✓ stream_config.yaml found"
else
    echo "  ✗ stream_config.yaml NOT FOUND"
fi
echo ""

# Generate summary report
cat > ANALYSIS_REPORT.md << EOF
# Executable Analysis Report

## File Information
- **Executable:** $EXEC_NAME
- **Path:** $EXECUTABLE
- **Size:** $(ls -lh "$EXECUTABLE" | awk '{print $5}')
- **Analysis Date:** $(date)

## Extraction Summary
- **Extracted to:** $EXTRACTED_DIR
- **Total files:** $TOTAL_FILES
- **Total directories:** $TOTAL_DIRS
- **Python modules (.pyc):** $MODULE_COUNT
- **Data files:** $DATA_COUNT

## Required Module Check

### Custom Modules (Critical)
EOF

for mod in "${REQUIRED_MODULES[@]}"; do
    RESULTS=$(find "$EXTRACTED_DIR" -name "*${mod}*" 2>/dev/null)
    if [ -n "$RESULTS" ]; then
        echo "- ✓ **${mod}** - FOUND" >> ANALYSIS_REPORT.md
    else
        echo "- ✗ **${mod}** - NOT FOUND ⚠️" >> ANALYSIS_REPORT.md
    fi
done

cat >> ANALYSIS_REPORT.md << EOF

### Standard Dependencies
EOF

for mod in "${STD_MODULES[@]}"; do
    if find "$EXTRACTED_DIR" -name "*${mod}*" 2>/dev/null | grep -q .; then
        echo "- ✓ ${mod}" >> ANALYSIS_REPORT.md
    else
        echo "- ✗ ${mod} ⚠️" >> ANALYSIS_REPORT.md
    fi
done

cat >> ANALYSIS_REPORT.md << EOF

## Files Generated
- \`file_listing.txt\` - Complete directory tree
- \`python_modules.txt\` - All .pyc files
- \`data_files.txt\` - All non-Python files
- \`ANALYSIS_REPORT.md\` - This report

## Detailed Custom Module Locations
EOF

for mod in "${REQUIRED_MODULES[@]}"; do
    echo "" >> ANALYSIS_REPORT.md
    echo "### ${mod}" >> ANALYSIS_REPORT.md
    echo "\`\`\`" >> ANALYSIS_REPORT.md
    find "$EXTRACTED_DIR" -name "*${mod}*" 2>/dev/null >> ANALYSIS_REPORT.md || echo "NOT FOUND" >> ANALYSIS_REPORT.md
    echo "\`\`\`" >> ANALYSIS_REPORT.md
done

cat >> ANALYSIS_REPORT.md << EOF

## Summary

**Status:** $( [ $MISSING_COUNT -eq 0 ] && echo "✓ ALL REQUIRED MODULES PRESENT" || echo "⚠️ MISSING $MISSING_COUNT MODULES" )

EOF

if [ $MISSING_COUNT -gt 0 ]; then
    cat >> ANALYSIS_REPORT.md << EOF
### Issues Detected

This executable is missing critical modules and will likely fail at runtime with ImportError.

**Recommended fixes:**
1. Add missing modules to PyInstaller with \`--hidden-import\` or \`--add-data\`
2. Verify module search paths with \`--paths\`
3. Review PyInstaller build log for warnings
4. Run full diagnostic: \`./tools/diagnose_pyinstaller.sh\`

EOF
else
    cat >> ANALYSIS_REPORT.md << EOF
### Status: OK

All required modules appear to be present in the executable bundle.

If the application still fails:
1. Check for runtime errors in the application log
2. Verify version compatibility of bundled libraries
3. Test module imports with self_diagnostic.py

EOF
fi

echo "========================================"
echo "ANALYSIS COMPLETE"
echo "========================================"
echo ""
echo "Summary:"
echo "  Required modules found: $FOUND_COUNT/${#REQUIRED_MODULES[@]}"
echo "  Required modules missing: $MISSING_COUNT"
echo ""
if [ $MISSING_COUNT -eq 0 ]; then
    echo "  Status: ✓ ALL MODULES PRESENT"
else
    echo "  Status: ⚠️ MISSING MODULES - LIKELY TO FAIL"
fi
echo ""
echo "Results saved to: $ANALYSIS_DIR"
echo ""
echo "View the report:"
echo "  cat $ANALYSIS_DIR/ANALYSIS_REPORT.md"
echo ""
echo "View extracted files:"
echo "  ls -R $ANALYSIS_DIR/$EXTRACTED_DIR/"
echo ""
