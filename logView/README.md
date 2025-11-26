# umod4 Log Viewer v2.0

Web-based viewer for umod4 motorcycle data logger binary logs.

## What's New in v2.0

**Major refactoring to ES6 modules for better maintainability and extensibility.**

### Modular Architecture

The viewer is now split into focused modules:

- **constants.js** - LOGID definitions and lookup tables
- **parser.js** - Binary log parsing and timestamp reconstruction
- **textRenderer.js** - HTML rendering with binary display
- **search.js** - Text search with highlighting
- **fileManager.js** - File I/O and recent files management

This makes it much easier to add new features like graphing and GPS integration.

## Usage

### For Phones/Tablets/USB Drives

**Use the bundled viewer** - works with `file://` protocol:

```bash
# Build the bundled version
make bundle

# Copy to your device
viewer_bundle.html  # Single file, no web server needed
```

Open `viewer_bundle.html` directly in any browser (Chrome, Firefox, Safari, Edge).

### For Development (Desktop)

**Use the modular viewer** with a web server:

```bash
# Start development server
make serve

# Open browser to:
http://localhost:8000/index.html
```

Edit files in `js/` directory, then run `make bundle` to create distributable version.

## Features

### Current (v2.0)
- ✅ Parse all 70+ LOGID event types (ECU, EP, WP)
- ✅ 16-bit to 64-bit timestamp reconstruction
- ✅ Binary data display with hex addresses
- ✅ Real-time text search with highlighting
- ✅ Recent files tracking (localStorage)
- ✅ Drag-and-drop file loading
- ✅ No build step - native ES6 modules

### Coming Soon (v2.1)
- 📋 Pop-out text view synced with graph
- 📋 Interactive time-series graphing (Canvas)
- 📋 GPS waypoints on graph
- 📋 Google Maps integration
- 📋 KML export for Google Earth

See [ROADMAP.md](./ROADMAP.md) for full feature plan.

## File Structure

```
logView/
├── index.html               Main viewer (ES6 modules)
├── viewer_prototype.html    Legacy single-file viewer
├── viewer_bundle.html       Bundled version (generated)
├── js/                      ES6 modules
│   ├── constants.js
│   ├── parser.js
│   ├── textRenderer.js
│   ├── search.js
│   └── fileManager.js
├── bundle.py                Bundler script
├── Makefile                 Build automation
├── CMakeLists.txt           CMake integration
├── ROADMAP.md               Feature plan
├── README.md                This file
└── STATUS.md                Implementation status
```

## Build System

### Standalone (Makefile)

```bash
make bundle   # Create viewer_bundle.html
make serve    # Start dev server
make clean    # Remove generated files
make help     # Show all commands
```

### Integrated (CMake)

The logView bundler integrates with the main umod4 build system:

```bash
cd ~/projects/umod4/build
cmake ..
ninja logViewer  # Or just 'ninja' to build everything

# Output: build/viewer_bundle.html
```

## Browser Compatibility

**Bundled viewer (viewer_bundle.html):**
- Chrome 61+ ✅
- Firefox 60+ ✅
- Safari 11+ ✅
- Edge 79+ ✅
- Mobile browsers ✅
- Works with `file://` protocol ✅

**Modular viewer (index.html):**
- Same browser support
- Requires web server (http://)

**Legacy viewer (viewer_prototype.html):**
- Works in all browsers
- Single file, no build needed

## Development

### Adding New Features

1. Create new module in `js/` directory
2. Export functions/classes
3. Import in `index.html` or other modules
4. Update ROADMAP.md if adding major feature

Example:
```javascript
// js/myFeature.js
export function doSomething() {
    // ...
}

// index.html
import { doSomething } from './js/myFeature.js';
```

### Testing

Load a test log file and verify:
- All events decode correctly
- Search works with large files
- Binary display toggles properly
- No console errors

## Log File Format

Binary format with LOGID byte followed by 0+ data bytes:

```
LOGID (1 byte) + DATA (0-N bytes)
```

Event types:
- **GEN** (0x00-0x0F): Version info
- **ECU** (0x10-0xCF): Engine data (sensors, timing, ignition, fuel)
- **EP** (0xD0-0xDF): EPROM loader events
- **WP** (0xE0-0xFF): GPS, filesystem, RTC

See `lib/inc/log_base.h` for full specification.

## Known Issues

### Search Performance
Search is slow with large files (>100k events) because it highlights all matches in DOM.

**Workaround:** Use smaller log files or wait for v2.2 virtual scrolling.

**Fix planned:** Phase 5.1 - Only highlight visible records.

### GPS Data Format
GPS lat/lon are currently displayed as raw int32 values, not converted to degrees.

**Fix planned:** Phase 2.1 - Proper coordinate conversion.

## Contributing

This is a personal project, but suggestions welcome!

If you find bugs or have feature ideas:
1. Check ROADMAP.md to see if already planned
2. Open an issue with details
3. Include example log file if possible

## License

Part of umod4 project - see top-level LICENSE file.
