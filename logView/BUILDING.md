# Building the Log Viewer

## Quick Start

```bash
cd /home/robin/projects/umod4/logView

# Create bundled version for phones/tablets
make bundle

# Result: viewer_bundle.html (53KB single file)
```

## What is Bundling?

The log viewer is developed as **ES6 modules** (separate JavaScript files) for maintainability, but ES6 modules don't work with `file://` protocol.

**Bundling** combines all modules into a single HTML file that works everywhere:
- ✅ Phones/tablets (open directly)
- ✅ USB drives (no web server needed)
- ✅ Email attachments
- ✅ Offline use

## Development Workflow

### 1. Development (Modular)
```bash
# Start web server for testing
make serve

# Open browser to http://localhost:8000/index.html
# Edit js/*.js files
```

### 2. Build (Bundle)
```bash
# Bundle modules into single file
make bundle

# Test the bundle
# Open viewer_bundle.html directly in browser
```

### 3. Distribute
```bash
# Copy to phone
scp viewer_bundle.html phone:/sdcard/Download/

# Or copy to USB drive
cp viewer_bundle.html /media/usb/

# Or email
# Attach viewer_bundle.html
```

## Build Targets

### Standalone (Makefile)

```bash
make bundle   # Create viewer_bundle.html from modules
make serve    # Start dev server on http://localhost:8000
make clean    # Remove generated files
make help     # Show all commands
```

### Integrated (CMake)

Add to top-level CMakeLists.txt:

```cmake
# In ~/projects/umod4/CMakeLists.txt
add_subdirectory(logView)
```

Then:

```bash
cd ~/projects/umod4/build
cmake ..
ninja logViewer

# Output:
#   build/viewer_bundle.html
#   build/logView/viewer_bundle.html
```

## How Bundling Works

The `bundle.py` script:

1. Reads `index.html`
2. Finds `<script type="module">` section
3. Recursively resolves all `import` statements
4. Inlines module code
5. Removes `export` statements
6. Outputs single `<script>` (no `type="module"`)

**Result:** All JavaScript in one file, no `import` statements, works with `file://`.

## File Sizes

```
index.html           ~30 KB  (plus separate .js files)
js/constants.js      ~3 KB
js/parser.js         ~23 KB
js/textRenderer.js   ~2 KB
js/search.js         ~6 KB
js/fileManager.js    ~2 KB
--------------------------------
viewer_bundle.html   ~53 KB  (everything combined)
```

The bundled file is only slightly larger than the sum of parts due to:
- No compression (for readability)
- Bundler comments (`// ===== Bundled: module.js =====`)
- Some code duplication during inlining

## Comparison to Other Approaches

| Approach | File Size | Phone Support | Build Step | Maintainability |
|----------|-----------|---------------|------------|-----------------|
| Single file (prototype) | 90 KB | ✅ Yes | ❌ No | 😞 Hard (1500+ lines) |
| ES6 modules (index.html) | 66 KB total | ❌ No | ❌ No | 😊 Easy (5 files) |
| **Bundled (recommended)** | **53 KB** | **✅ Yes** | **✅ Simple** | **😊 Easy** |
| Webpack/esbuild | 45 KB | ✅ Yes | ⚠️ Complex | 😊 Easy |

## Future: CMake Integration

Once CMake integration is added (already prepared), building is automatic:

```bash
ninja  # Builds everything including logViewer
```

The bundled viewer will appear in:
- `build/viewer_bundle.html`
- Ready to copy to phone/tablet

## Testing the Bundle

### Manual Test
```bash
# Bundle
make bundle

# Open directly (file:// protocol)
firefox viewer_bundle.html

# Or
google-chrome viewer_bundle.html

# Should work identically to the modular version
```

### Automated Test (Future)
```python
# tools/test_viewer.py
import subprocess

def test_bundle_creates_file():
    subprocess.run(['make', 'bundle'], check=True)
    assert Path('viewer_bundle.html').exists()
    assert Path('viewer_bundle.html').stat().st_size > 50_000
```

## Troubleshooting

### "Module import failed"
You're trying to open `index.html` with `file://` - use `viewer_bundle.html` instead.

### "Bundle script fails"
Check Python version: `python3 --version` (need 3.6+)

### "Bundle works on desktop but not phone"
Check browser console on phone - some old mobile browsers don't support modern JavaScript.

**Workaround:** Use the legacy `viewer_prototype.html` which uses older JavaScript.

## Contributing

When adding new modules:

1. Create `js/newModule.js`
2. Export functions: `export function foo() { }`
3. Import in `index.html`: `import { foo } from './js/newModule.js'`
4. Run `make bundle` - bundler automatically includes new module
5. Test bundled version

No changes to `bundle.py` needed unless you change the import syntax.
