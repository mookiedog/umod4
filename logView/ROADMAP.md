# umod4 Log Viewer - Feature Roadmap

## Current Status: v2.0 Modular Architecture ✅

**Completed Modularization** - The viewer has been refactored from a single 1500-line file into clean ES6 modules:

### Module Structure
```
logView/
├── index.html              (~600 lines - Main viewer shell)
├── js/
│   ├── constants.js       (~100 lines - LOGID definitions, lookup tables)
│   ├── parser.js          (~700 lines - parseLog, TimeKeeper, binary parsing)
│   ├── textRenderer.js    (~60 lines - renderRecords, binary formatting)
│   ├── search.js          (~200 lines - search, highlighting, navigation)
│   └── fileManager.js     (~60 lines - file I/O, recent files, utils)
```

**Total:** ~1720 lines across 6 files (vs 1500 lines in single file)

### Current Features
- ✅ Parse binary logs with all 70+ event types
- ✅ Timestamp reconstruction (16-bit → 64-bit nanoseconds)
- ✅ Binary data display with hex addresses
- ✅ Text search with highlighting
- ✅ Recent files tracking
- ✅ Drag-and-drop file loading
- ✅ ES6 module architecture (easy to extend)

---

## Phase 1: Synced Text View Window (Priority: High)

**Goal:** Open decoded text log in separate window synced with graph viewport

### Features
- Button: "📄 Pop out Text View"
- Opens new window with text-only view
- Two-way synchronization:
  - Clicking graph scrolls text to that timestamp
  - Searching text jumps graph to that location
- Toggle button: "🔗 Sync with Graph" (enable/disable sync)

### Implementation
**New files:**
- `text_view.html` - Standalone text viewer window
- `js/windowSync.js` - Window-to-window communication via postMessage

**Changes:**
- `index.html` - Add "Pop out" button
- `textRenderer.js` - Make reusable by both windows

**Estimated:** 200 lines of new code

**Benefits:**
- Side-by-side text + graph view
- Better for deep log analysis
- Reuses existing text rendering code

---

## Phase 2: GPS Integration (Priority: High)

**Goal:** Display GPS waypoints on graph and open Google Maps/Earth

### 2.1 GPS Data Extraction
**Task:** Parse and extract GPS position/velocity records

**Implementation:**
- `js/gpsExtractor.js` - Extract GPS records from parsed data
- Store: `{timestamp, lat, lon, velocity, fixType}[]`

**Estimated:** 100 lines

### 2.2 GPS Points on Graph
**Task:** Render GPS waypoints as clickable markers

**Features:**
- Green dots for good GPS fix, yellow for poor fix
- Hover tooltip: lat/lon, velocity, timestamp
- Click opens context menu

**Implementation:**
- Add to graphing module (Phase 3)
- Toggle layer: "Show GPS Points"

**Estimated:** 150 lines (part of graphing)

### 2.3 Google Maps Integration
**Task:** Right-click GPS point → Open in Google Maps

**Implementation:**
- `js/gpsExport.js` - Google Maps URL generation
- Context menu on GPS points:
  - 🗺️ Open in Google Maps
  - 🌍 Export GPS Track (KML)
  - 📄 Show in Text Log

**Example URL:**
```javascript
function openGoogleMaps(lat, lon) {
    const url = `https://www.google.com/maps/search/?api=1&query=${lat},${lon}`;
    window.open(url, '_blank');
}
```

**Estimated:** 50 lines

### 2.4 GPS Track Export (KML)
**Task:** Export entire GPS track to Google Earth format

**Features:**
- Button: "🌍 Export GPS Track"
- Generate KML with:
  - Full GPS path (LineString)
  - Waypoint markers with speed/timestamp
  - Color-coded by speed

**Implementation:**
```javascript
function generateKML(gpsPoints) {
    // Returns KML XML string
    // Downloads as .kml file
}
```

**Estimated:** 200 lines

**Total GPS Phase:** ~500 lines

---

## Phase 3: Interactive Graphing (Priority: Critical)

**Goal:** Port Python viz.py to JavaScript with Canvas/WebGL

### Architecture Decision
**Option A: Pure Canvas 2D** (Recommended)
- Pros: Simple, no dependencies, works everywhere
- Cons: Slower for >100k points

**Option B: Canvas + OffscreenCanvas**
- Pros: Better performance, can use Web Workers
- Cons: More complex

**Option C: WebGL (via library like Plot.js or D3)**
- Pros: Very fast, handles millions of points
- Cons: Complex, larger bundle

**Recommendation:** Start with Option A, upgrade to B if needed.

### 3.1 Graph Module Foundation
**File:** `js/graphing.js` (~800 lines)

**Features:**
- Multi-trace plotting (RPM, throttle, MAP, etc.)
- Pan and zoom
- Vertical crosshair with value readout
- Time axis (X) with nanosecond precision
- Multiple Y-axes (left/right for different scales)

**Implementation:**
```javascript
export class GraphRenderer {
    constructor(canvasElement) {
        this.canvas = canvasElement;
        this.ctx = canvas.getContext('2d');
        this.traces = [];  // {name, data[], color, yAxis}
        this.viewport = {xMin, xMax, yMin, yMax};
    }

    addTrace(name, dataPoints, options) { }
    render() { }
    handleZoom(event) { }
    handlePan(event) { }
    getValueAtTime(timestamp) { }
}
```

**Data Structure:**
```javascript
// Extract from parsed records
const traces = {
    rpm: [{t: 0.001, v: 1500}, {t: 0.002, v: 1520}, ...],
    map: [{t: 0.001, v: 85}, {t: 0.002, v: 87}, ...],
    vta: [{t: 0.001, v: 150}, {t: 0.002, v: 155}, ...]
};
```

**Estimated:** 800 lines

### 3.2 Trace Selection UI
**Features:**
- Checkboxes for each event type
- Color picker for each trace
- Y-axis assignment (left/right)
- Grouping: Sensors, Ignition, Fuel, GPS, etc.

**Implementation:**
- Add panel to index.html
- `js/traceManager.js` - Manage visible traces

**Estimated:** 200 lines

### 3.3 Performance Optimization
**Task:** Handle large files (100k+ events)

**Techniques:**
- Downsampling for zoomed-out views (Douglas-Peucker algorithm)
- Only render visible data points
- OffscreenCanvas for background rendering
- Incremental rendering (render in chunks)

**Estimated:** 300 lines

**Total Graphing Phase:** ~1300 lines

---

## Phase 4: Advanced Features (Priority: Medium)

### 4.1 Event Filtering
- Filter by event type (show only GPS, only sensors, etc.)
- Time range selection
- Export filtered log

**Estimated:** 200 lines

### 4.2 Calculated Channels
- Derive new channels from existing data
- Examples:
  - Engine speed (from crank events)
  - AFR (air-fuel ratio from MAP + throttle)
  - Acceleration (from GPS velocity)

**Estimated:** 300 lines

### 4.3 Annotations
- Add markers/notes to specific timestamps
- Save annotations to localStorage
- Export annotations with log

**Estimated:** 150 lines

### 4.4 Multi-File Comparison
- Load 2+ log files
- Overlay graphs for comparison
- Sync timelines

**Estimated:** 400 lines

**Total Advanced Phase:** ~1050 lines

---

## Phase 5: Performance & Polish (Priority: Low)

### 5.1 Virtual Scrolling
- Only render visible log records in DOM
- Dramatically improves search performance
- Use Intersection Observer API

**Estimated:** 250 lines

### 5.2 Web Worker for Parsing
- Move parseLog() to Web Worker
- Keep UI responsive during parse
- Show progress bar

**Estimated:** 150 lines

### 5.3 Progressive Web App (PWA)
- Service worker for offline use
- Cache log files locally
- Install as desktop app

**Estimated:** 200 lines

**Total Polish Phase:** ~600 lines

---

## Development Priorities

### Must-Have (v2.1 - Next Release)
1. ✅ Modular architecture (DONE)
2. **Synced Text View Window** (Phase 1)
3. **Basic Graphing** (Phase 3.1 + 3.2)
4. **GPS Points on Graph** (Phase 2.2)

**Target:** ~1500 new lines, 2-3 weeks of development

### Should-Have (v2.2)
5. **Google Maps Integration** (Phase 2.3 + 2.4)
6. **Graph Performance** (Phase 3.3)
7. **Event Filtering** (Phase 4.1)

**Target:** ~800 new lines, 1-2 weeks

### Nice-to-Have (v3.0)
8. **Calculated Channels** (Phase 4.2)
9. **Virtual Scrolling** (Phase 5.1)
10. **Multi-File Comparison** (Phase 4.4)

**Target:** ~950 new lines, 2-3 weeks

---

## Estimated Total Scope

| Phase | Lines of Code | Priority | Status |
|-------|--------------|----------|--------|
| Modularization | 220 (refactor) | Critical | ✅ Done |
| Phase 1: Text Window | 200 | High | 📋 Planned |
| Phase 2: GPS | 500 | High | 📋 Planned |
| Phase 3: Graphing | 1300 | Critical | 📋 Planned |
| Phase 4: Advanced | 1050 | Medium | 📋 Future |
| Phase 5: Polish | 600 | Low | 📋 Future |
| **Total** | **~3850** | | |

---

## Technical Decisions Log

### Why ES6 Modules?
- Native browser support (no build step for development)
- Clean separation of concerns
- Easy to test individual modules
- Can add bundler later if needed (Vite/Rollup)

### Why Canvas over SVG for Graphing?
- Better performance for large datasets (>10k points)
- Lower memory footprint
- Easier to implement custom rendering optimizations
- Can still use SVG for UI overlays

### Why localStorage over IndexedDB?
- Simpler API for small data (recent files, settings)
- Can upgrade to IndexedDB later for large annotations

### Why No Framework (React/Vue)?
- Keep bundle size small
- Vanilla JS is sufficient for this use case
- Easier for motorcycle enthusiasts to contribute
- Can load as single HTML file from USB drive

---

## Testing Strategy

### Manual Testing Checklist
- [ ] Load log file with 100k+ events
- [ ] Search performance with large files
- [ ] Binary display toggles correctly
- [ ] Graph zooming/panning smooth
- [ ] GPS points clickable
- [ ] Text view sync works
- [ ] Works in Chrome, Firefox, Safari, Edge
- [ ] Works offline

### Test Log Files Needed
1. Small file (~1k events) - Quick smoke test
2. Medium file (~50k events) - Normal use case
3. Large file (~500k events) - Stress test
4. GPS-enabled file - Test GPS features
5. Multi-session file - Test timestamp wraparound

---

## Future Ideas (Beyond Roadmap)

- Real-time log streaming from WP over WiFi
- Mobile-responsive UI for tablet viewing in garage
- Export graphs as PNG/PDF for reports
- Bluetooth OBD-II integration for modern bikes
- Community log sharing platform
- Machine learning for anomaly detection

---

**Last Updated:** 2025-11-26
**Version:** 2.0 Modular
**Next Milestone:** v2.1 - Text Window + Basic Graphing
