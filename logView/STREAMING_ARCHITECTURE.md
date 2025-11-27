# umod4 Log Viewer - Streaming Architecture

## Overview

The umod4 log viewer uses a **client-server streaming architecture** to handle large log files that would otherwise consume excessive memory when loaded entirely in the browser.

**Problem:** A 1MB binary log file expands to ~8GB of RAM when parsed into JavaScript objects. A full day's ride would require 800GB+ RAM - completely infeasible.

**Solution:** A local Python server streams log chunks on-demand as the user scrolls, keeping browser memory usage constant regardless of file size.

## Architecture Components

### Current Phase: Local File Streaming

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   Browser   │◄───────►│ Python Server│◄───────►│  Log Files  │
│             │  HTTP   │ (localhost)  │  Parse  │  (SD card)  │
│ Virtual     │  Chunks │              │  Chunks │             │
│ Scrolling   │         │ Streams 200  │         │   GB-sized  │
│ Viewport    │         │ events/req   │         │   files OK  │
└─────────────┘         └──────────────┘         └─────────────┘
```

**User Workflow:**
1. Copy log files from SD card to laptop
2. Run: `python3 logserver.py ~/logs/2025-11-27/log.1`
3. Browser opens automatically to `http://localhost:5000`
4. Scroll/search through unlimited-size logs smoothly
5. Close browser when done

**Key Benefits:**
- ✅ Free (runs locally, no hosting costs)
- ✅ Offline (no internet required)
- ✅ Unlimited file size (GB+ files work fine)
- ✅ Constant RAM usage (~100MB browser)
- ✅ Reuses existing Python decoder
- ✅ Cross-platform (Windows/Linux/macOS)

## Future Phase: Automatic WiFi Upload

### Home Network Auto-Upload

When you return from a ride and plug the umod4 board into USB power (with bike ECU off), the Pico2W will:

1. Detect USB power + ECU off → Enter WiFi mode
2. Connect to home WiFi network
3. Upload latest ride log to known network location (e.g., `~/umod4/uploads/`)
4. Python server (running on home network) auto-detects new file
5. Browser shows notification: "📁 New ride log available!"

```
Park bike → Plug USB → umod4 WiFi uploads
                              ↓
                    ┌─────────────────┐
                    │  Home Network   │
                    │  File Share     │
                    │ ~/umod4/uploads/│
                    └────────┬────────┘
                             ↓
                    ┌─────────────────┐
                    │ Python Server   │
                    │ (File Watcher)  │
                    └────────┬────────┘
                             ↓
                    ┌─────────────────┐
                    │   Browser       │
                    │  Auto-opens log │
                    └─────────────────┘
```

**Server Enhancement:**
```python
# logserver.py gains file watcher
import watchdog

class LogWatcher:
    def on_new_file(self, filepath):
        # Auto-index new log
        # Notify connected browsers via WebSocket
        # Browser shows: "📁 New log: 2025-11-27_ride.log [View]"
```

### Direct Access Point Mode

If home WiFi is unavailable (e.g., in the field), the umod4 can create its own access point:

1. No home WiFi detected → umod4 creates AP: `umod4-XXXX`
2. Laptop connects to this AP
3. umod4 scans for logserver via mDNS (service discovery)
4. If found, umod4 uploads log via HTTP POST
5. Server auto-indexes and opens in browser

```
No WiFi → umod4 creates AP "umod4-XXXX"
                    ↓
          Laptop connects to AP
                    ↓
     ┌──────────────┴──────────────┐
     │  mDNS Discovery              │
     │  umod4 finds: _umod4server   │
     │  @ 192.168.4.1:5000          │
     └──────────────┬──────────────┘
                    ↓
          HTTP POST to /upload
                    ↓
          Server receives & indexes
                    ↓
          Browser opens log view
```

**Discovery Protocol:**

**umod4 (Pico2W C code):**
```c
// Scan for mDNS service: _umod4server._tcp.local
// If found, POST log to discovered IP:port
```

**Laptop (Python server):**
```python
from zeroconf import ServiceInfo, Zeroconf

# Advertise: "umod4-logserver @ IP:5000"
# umod4 discovers this and uploads
```

**Server Upload Endpoint:**
```python
@app.route('/upload', methods=['POST'])
def upload_log():
    file = request.files['log']
    filepath = save_upload(file)
    index_log(filepath)
    return jsonify({'url': f'/view/{filepath}'})
```

## Even Further Future: Live Streaming

For real-time debugging while the bike is running:

1. Bike running, umod4 powered and connected to laptop AP
2. umod4 streams log data in real-time over WiFi
3. Server receives chunks, buffers recent events
4. Browser shows live tail view
5. Can trigger "save last 30 seconds" to capture specific events

```
┌──────────┐  WiFi   ┌────────────┐  WebSocket  ┌─────────┐
│  umod4   │────────►│   Server   │────────────►│ Browser │
│  (Bike)  │ Stream  │  (Laptop)  │  Real-time  │  Live   │
│ Running  │  Chunks │   Buffer   │    View     │  Tail   │
└──────────┘         └────────────┘             └─────────┘
```

## Implementation Phases

### Phase 1: Basic Streaming (Now)
**What to Build:**
- Local Python server that streams log chunks
- Modified browser viewer to fetch chunks on scroll
- Uses existing decodelog.py parser
- Simple file selection in browser

**Files:**
- `logView/logserver.py` - Streaming server
- `logView/js/streamingRenderer.js` - Modified virtual renderer
- `logView/index_streaming.html` - Server-mode viewer

**Usage:**
```bash
cd ~/projects/umod4/logView
python3 logserver.py ~/logs/file.log
# Opens browser to http://localhost:5000
```

### Phase 2: WiFi Upload Support (When umod4 WiFi Ready)
**What to Add:**
- File upload endpoint (`/upload`)
- Directory watcher (auto-detects new files)
- WebSocket notifications (browser alerts)
- mDNS service advertisement

**New Server Features:**
```python
# Server gains:
@app.route('/upload', methods=['POST'])  # Receive uploads
class FileWatcher:                        # Auto-detect new files
class WebSocketNotifier:                  # Alert browsers
advertise_mdns("_umod4server._tcp")      # Discovery
```

**umod4 Pico2W Code:**
```c
// Detect USB power + ECU off
if (usb_powered && !ecu_powered) {
    wifi_connect_or_create_ap();

    if (wifi_connected_to_home) {
        upload_to_network_share();
    } else if (ap_mode) {
        mdns_find_server();
        if (server_found) {
            http_post_log(server_ip);
        }
    }
}
```

### Phase 3: Live Streaming (Later)
**What to Add:**
- `/stream/live` endpoint
- Real-time chunk buffering
- Live tail viewer mode
- "Capture last N seconds" feature

**Server Enhancement:**
```python
@app.route('/stream/live')
def stream_live():
    # Server-sent events or WebSocket
    # Stream incoming chunks to browser

@app.route('/capture')
def capture_buffer():
    # Save last N seconds to file
```

## Technical Details

### Chunk Streaming Protocol

**Browser Request:**
```http
GET /events?start=1000&end=1200 HTTP/1.1
```

**Server Response:**
```json
{
  "records": [
    {"index": 1000, "time_ns": 8589900000, "html": "...", "binData": [...], "binOffset": 42},
    {"index": 1001, "time_ns": 8589902000, "html": "...", "binData": [...], "binOffset": 43},
    ...
  ],
  "total": 350000
}
```

**Browser Integration:**
```javascript
class StreamingRenderer extends VirtualRenderer {
    async updateVisibleRange() {
        // Fetch only visible chunk from server
        const response = await fetch(
            `/events?start=${this.visibleStart}&end=${this.visibleEnd}`
        );
        const data = await response.json();
        this.renderRecords(data.records);
    }
}
```

### Server Log Indexing

To enable fast random access, the server builds an index on file open:

```python
class LogIndexer:
    def __init__(self, filepath):
        self.file = open(filepath, 'rb')
        self.index = []  # List of (record_num, file_offset, timestamp)
        self.build_index()

    def build_index(self):
        # Scan file, record offset of each event
        # Store: [(0, 0, ts0), (1, 42, ts1), (2, 87, ts2), ...]

    def get_range(self, start, end):
        # Seek to self.index[start][1] (file offset)
        # Parse events [start:end]
        # Return as JSON array
```

**Index Build Time:** ~1 second per 100MB file (one-time cost on open)

### Search Implementation

**Server-Side Search:**
```python
@app.route('/search')
def search():
    term = request.args.get('q')
    matches = []

    # Stream through file, test each event
    for idx, event in enumerate(stream_events()):
        if term.lower() in event.text.lower():
            matches.append(idx)

    return jsonify({'matches': matches})
```

**Browser:**
```javascript
async performSearch(searchTerm) {
    const response = await fetch(`/search?q=${encodeURIComponent(searchTerm)}`);
    const data = await response.json();
    this.searchMatches = data.matches;  // List of matching indices
    this.scrollToMatch(0);               // Jump to first match
}
```

## Hardware: Pico2W WiFi

The umod4 uses a **Raspberry Pi Pico2W** (not ESP32) for WiFi capabilities.

**Pico2W Specifications:**
- Dual ARM Cortex-M33 cores @ 150MHz
- 520KB SRAM
- Infineon CYW43439 WiFi 4 chip (802.11n)
- Supports STA (station) and AP (access point) modes
- mDNS/Bonjour support via lwIP stack

**C SDK WiFi Example:**
```c
#include "pico/cyw43_arch.h"

// Initialize WiFi
cyw43_arch_init();

// Connect to home network
cyw43_arch_wifi_connect_timeout_ms(
    "HomeSSID",
    "password",
    CYW43_AUTH_WPA2_AES_PSK,
    10000
);

// Or create AP
cyw43_arch_enable_ap_mode(
    "umod4-XXXX",
    "password",
    CYW43_AUTH_WPA2_AES_PSK
);
```

**File Upload (lwIP HTTP client):**
```c
// POST log file to server
http_client_post(
    "http://192.168.1.100:5000/upload",
    file_data,
    file_size,
    upload_complete_callback
);
```

## Benefits Summary

### For Current Use (Manual SD Card)
- Handle unlimited file sizes (GB+)
- Constant browser memory (~100MB)
- Smooth scrolling/searching
- No internet required
- Free (runs locally)

### For Future WiFi Auto-Upload
- No manual SD card removal
- Automatic upload on return home
- Browser notification when log ready
- Works offline on home network

### For Field Use (Direct AP)
- No home network needed
- Direct umod4 → laptop connection
- Auto-discovery via mDNS
- Instant log viewing

### For Debugging (Live Stream)
- Real-time log viewing while bike runs
- Capture specific events
- No need to stop/download

## Deployment

### Desktop Users
**Option 1: Python Script (Simple)**
```bash
# Install dependencies (one-time)
pip install flask watchdog zeroconf

# Run server
python3 logserver.py [logfile]
```

**Option 2: Electron App (Polished)**
- Package Python + server + viewer as desktop app
- One-click launcher
- Drag-and-drop files
- Recent files menu
- Auto-update support

### Mobile Users
Continue using existing bundled viewer (`viewer_bundle.html`) for small field-check logs (< 10MB). Add file size warning for larger files.

## Migration Path

1. **Keep existing bundle** - Still works for small files, no changes needed
2. **Build Phase 1 server** - For large files on desktop
3. **Add WiFi upload** - When Pico2W firmware ready
4. **Add live streaming** - For advanced debugging

Each phase is independent and additive - no breaking changes to existing workflow.

## Summary

The streaming architecture solves the fundamental memory limitation of client-side log parsing while setting up a flexible foundation for future WiFi features. The local server approach:

- ✅ Handles unlimited file sizes
- ✅ Costs nothing (no cloud hosting)
- ✅ Works offline
- ✅ Reuses existing decoder
- ✅ Enables automatic WiFi upload
- ✅ Supports direct device connection
- ✅ Allows future live streaming

The Pico2W WiFi module provides the hardware foundation for automatic log upload and direct laptop connection, making the post-ride workflow seamless.
