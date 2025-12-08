#!/bin/bash
# Build script for creating visualizer releases
# Usage: ./tools/build-release.sh v2.1.0

set -e

VERSION=$1
if [ -z "$VERSION" ]; then
  echo "Usage: ./tools/build-release.sh <version>"
  echo "Example: ./tools/build-release.sh v2.1.0"
  exit 1
fi

# Ensure we're in the project root
if [ ! -f "CMakeLists.txt" ]; then
  echo "Error: Must run from project root directory"
  echo ""
  echo "Current directory: $(pwd)"
  echo "Expected to find: CMakeLists.txt"
  echo ""
  echo "Please run from the project root:"
  echo "  cd /path/to/umod4"
  echo "  ./tools/build-release.sh <version>"
  exit 1
fi

# Check if venv exists
if [ ! -e "build/.venv/bin/python3" ]; then
  echo "Error: Virtual environment not found at build/.venv"
  echo "Run CMake configure first to create the venv"
  exit 1
fi

PROJECT_ROOT="$(pwd)"
PYTHON="${PROJECT_ROOT}/build/.venv/bin/python3"
VIZ_DIR="${PROJECT_ROOT}/tools/logtools/viz"
DIST_DIR="${VIZ_DIR}/dist"
RELEASE_DIR="${PROJECT_ROOT}/build/releases/visualizer/${VERSION}"

echo "================================================"
echo "Building Data Visualizer Release ${VERSION}"
echo "================================================"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf "${VIZ_DIR}/dist" "${VIZ_DIR}/build" "${VIZ_DIR}/*.spec"
rm -rf "${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}"

# Install PyInstaller if not already installed
echo "Checking PyInstaller..."
if ! ${PYTHON} -c "import PyInstaller" 2>/dev/null; then
  echo "Installing PyInstaller..."
  ${PYTHON} -m pip install pyinstaller
fi

# Build Windows executable
echo ""
echo "Building Windows executable with PyInstaller..."
cd "${VIZ_DIR}"

${PYTHON} -m PyInstaller \
  --onefile \
  --windowed \
  --add-data "stream_config.py:." \
  --add-data "stream_config.yaml:." \
  --add-data "viz_components:viz_components" \
  --name "DataVisualizer" \
  --clean \
  viz.py

cd ../../..

# Check build succeeded
if [ ! -f "${DIST_DIR}/DataVisualizer.exe" ] && [ ! -f "${DIST_DIR}/DataVisualizer" ]; then
  echo "Error: Build failed - executable not found in ${DIST_DIR}"
  exit 1
fi

# Find the executable (name depends on platform)
if [ -f "${DIST_DIR}/DataVisualizer.exe" ]; then
  EXE_NAME="DataVisualizer.exe"
  PLATFORM="Windows"
else
  EXE_NAME="DataVisualizer"
  PLATFORM="$(uname)"
fi

echo ""
echo "Built: ${EXE_NAME} for ${PLATFORM}"
ls -lh "${DIST_DIR}/${EXE_NAME}"

# Copy sample log files from test data
echo ""
echo "Copying sample log files..."
for logfile in "${PROJECT_ROOT}"/tools/logtools/test_data/*.um4 "${PROJECT_ROOT}"/tools/logtools/test_data/*.log; do
  if [ -f "$logfile" ]; then
    filename=$(basename "$logfile")
    echo "  Including ${filename}..."
    cp "$logfile" "${RELEASE_DIR}/sample-${filename}"
  fi
done

# Copy executable to release directory
echo ""
echo "Packaging release..."
cp "${DIST_DIR}/${EXE_NAME}" "${RELEASE_DIR}/"

# Create README for the release
cat > "${RELEASE_DIR}/README.txt" << EOF
Data Visualizer ${VERSION}
=========================

QUICK START
-----------
1. Double-click DataVisualizer.exe
2. Open one of the sample .h5 files to see example data
3. Use File > Open to load your own .h5 or .um4 log files

SYSTEM REQUIREMENTS
-------------------
- Windows 10 or later
- No additional software needed

SAMPLE FILES
------------
$(ls -1 ${RELEASE_DIR}/sample-*.um4 ${RELEASE_DIR}/sample-*.log 2>/dev/null | sed 's/.*\//  - /')

These are raw binary log files. When you open them in the visualizer,
it will automatically offer to convert them to HDF5 format for viewing.

GETTING YOUR OWN LOGS
---------------------
1. Extract log files from your umod4 SD card (*.um4 or *.log files)
2. Open them directly in the visualizer
3. The visualizer will automatically convert them to HDF5 format

LINUX / MACOS USERS
-------------------
For the moment, this release is Windows-only. Linux and MacOS is coming.
If you *really* want to try it out beforehand, you would need to clone the 
umod4 repository and get it that way.

MORE INFORMATION
----------------
Project: https://github.com/mookiedog/umod4
Issues: https://github.com/mookiedog/umod4/issues

EOF

# Create ZIP archive
echo ""
echo "Creating release archive..."
ARCHIVE_NAME="DataVisualizer-${PLATFORM}-${VERSION}.zip"

if command -v zip >/dev/null 2>&1; then
  cd "${PROJECT_ROOT}/build/releases/visualizer"
  zip -q -r "${ARCHIVE_NAME}" "${VERSION}"
  cd "${PROJECT_ROOT}"
else
  echo "Warning: 'zip' command not found. Skipping archive creation."
  echo "You can manually create the archive with:"
  echo "  cd build/releases/visualizer && zip -r ${ARCHIVE_NAME} ${VERSION}"
  ARCHIVE_NAME="(not created - zip not available)"
fi

echo ""
echo "================================================"
echo "Release build complete!"
echo "================================================"
echo ""
if [ -f "build/releases/visualizer/${ARCHIVE_NAME}" ]; then
  echo "Archive: build/releases/visualizer/${ARCHIVE_NAME}"
  echo "Size: $(du -h build/releases/visualizer/${ARCHIVE_NAME} | cut -f1)"
else
  echo "Archive: ${ARCHIVE_NAME}"
fi
echo ""
echo "Contents:"
ls -lh "${RELEASE_DIR}"
echo ""
echo "NEXT STEPS:"
echo "==========="
echo "1. Test the executable:"
echo "   cd ${RELEASE_DIR} && ./${EXE_NAME}"
echo ""
echo "2. Create git tag:"
echo "   git tag viz-${VERSION}"
echo "   git push origin viz-${VERSION}"
echo ""
echo "3. Create GitHub Release:"
echo "   - Go to: https://github.com/mookiedog/umod4/releases/new"
echo "   - Select tag: viz-${VERSION}"
echo "   - Upload: build/releases/visualizer/${ARCHIVE_NAME}"
echo "   - Write release notes and publish"
echo ""
