# logView Features

## Current Features (Prototype v0.1)

### File Loading
- ✅ **File Picker** - Click "Open Log File" button
- ✅ **Drag & Drop** - Drag log files onto browser window
- ✅ **Recent Files** - Tracks last 10 opened files

### Recent Files System
Uses browser `localStorage` to persist data across sessions.

**What's Stored:**
- File name
- File size
- Last opened timestamp
- Kept for up to 10 most recent files

**Features:**
- Click "🕒 Recent Files" button to view list
- Shows file name, size, and when last opened
- Remove individual files from history (click ✕)
- Sorted by most recently opened first

**Limitations:**
Due to browser security, the viewer cannot automatically reload files from disk. Recent files are for **reference only** - you must manually reselect them via file picker or drag-and-drop.

### Data Display
- ✅ **Text View** - Human-readable event stream
- ✅ **Statistics** - File size, event count, duration, parse time
- ✅ **Color Coding** - Syntax highlighting for events and values
- ✅ **Tab Interface** - Switch between text and graph views

### Event Parsing
- ✅ **EP Events** - EPROM loading (LOAD_NAME, LOAD_ADDR, LOAD_LEN, LOAD_ERR)
- ✅ **ECU Events** - Sensors, injectors, coils, crank position
- ✅ **WP Events** - GPS velocity
- ✅ **Timestamp Reconstruction** - 16-bit → 64-bit nanoseconds

### User Interface
- ✅ **Responsive Design** - Works on desktop and mobile
- ✅ **Touch Friendly** - Tap, drag, swipe gestures
- ✅ **Dark Theme** - Output console with syntax highlighting
- ✅ **Clear Button** - Reset and load new file

## Upcoming Features

### Phase 2: Enhanced Text View
- [ ] Add all remaining event types (~70 more)
- [ ] Temperature conversion (ADC → °C)
- [ ] Binary formatting for PORTG
- [ ] Error bit decoding
- [ ] Remove 5000 event limit
- [ ] Search/filter events
- [ ] Export text view to file

### Phase 3: Graphical View
- [ ] Interactive time-series plots
- [ ] Stream selection
- [ ] Zoom and pan controls
- [ ] Multiple Y-axes
- [ ] Cursor/crosshair with value readout
- [ ] Auto-scale / fit to view
- [ ] Export plots as PNG

### Phase 4: Advanced Features
- [ ] Compare two log files side-by-side
- [ ] Custom event filters
- [ ] Bookmarks/markers
- [ ] Save/restore view state
- [ ] Statistics and analysis
- [ ] RPM histogram
- [ ] Temperature plots

### Phase 5: Settings Persistence
All settings will be saved to localStorage:
- [ ] Last opened directory path
- [ ] Preferred tab (text/graph)
- [ ] Selected streams for graphing
- [ ] Zoom/pan state
- [ ] Color theme preference
- [ ] Display units (metric/imperial)
- [ ] Event limit (5000 / 10000 / unlimited)

## Storage Limitations

### Browser localStorage
- **Capacity**: ~5-10MB depending on browser
- **What we store**:
  - Recent files list: ~2KB (10 files × 200 bytes)
  - Settings: ~1KB
  - Total: <5KB (well within limits)

### What's NOT Stored
- Log file contents (too large, loaded fresh each time)
- Parsed data (regenerated on each load)
- File handles (browser security prevents this)

## Privacy & Data

**All data stays local:**
- No server communication
- No analytics or tracking
- No file uploads
- Everything processed in your browser
- localStorage is per-domain, secure

**To clear all data:**
```javascript
// In browser console (F12)
localStorage.clear();
```

Or use browser settings: Clear Site Data

## Browser Compatibility

| Feature | Chrome | Firefox | Edge | Safari |
|---------|--------|---------|------|--------|
| localStorage | ✅ | ✅ | ✅ | ✅ |
| File API | ✅ | ✅ | ✅ | ✅ |
| Drag & Drop | ✅ | ✅ | ✅ | ✅ |
| WebAssembly | ✅ | ✅ | ✅ | ✅ |

**Minimum versions:**
- Chrome 76+
- Firefox 68+
- Edge 79+
- Safari 13+

## Future: Web Storage API

For larger cached data (parsed logs), we could use:
- **IndexedDB** - Store parsed data, 50MB+ capacity
- **Cache API** - Store processed results
- **File System Access API** - Request permission to keep reading same file (Chrome only)

This would enable:
- Instant reload of recently viewed files
- No re-parsing required
- Better performance for large logs

## Settings Example

Future settings interface:
```javascript
// Settings stored in localStorage
{
    "theme": "dark",
    "units": "metric",
    "eventLimit": 10000,
    "defaultTab": "text",
    "selectedStreams": ["ecu_rpm", "coolant_temp", "throttle"],
    "recentFiles": [
        {"name": "log.3", "size": 38912, "timestamp": 1732547280000},
        {"name": "log.5", "size": 42104, "timestamp": 1732546180000}
    ]
}
```

Access from browser console:
```javascript
// View current settings
console.log(localStorage.getItem('recentFiles'));

// Clear recent files only
localStorage.removeItem('recentFiles');

// Clear all settings
localStorage.clear();
```
