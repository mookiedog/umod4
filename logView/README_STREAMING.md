# umod4 Log Streaming Viewer

Version 3.0 - Server-side streaming for unlimited file sizes

## Overview

The streaming viewer solves the memory limitation problem by streaming log chunks from a local Python server on-demand. This allows viewing logs of any size (GB+) with constant browser memory usage.

## Quick Start

### 1. Install Dependencies (One-Time Setup)

Dependencies are automatically installed when you configure the CMake project:

```bash
cd ~/projects/umod4/build
cmake ..
# Or in VS Code: F1 -> "CMake: Configure"
```

This installs Flask and Flask-CORS (and all other Python dependencies) into the project's virtual environment at `build/.venv`.

### 2. Run Server

```bash
cd ~/projects/umod4/logView
./logserver.py <path-to-log-file>
```

Example:
```bash
./logserver.py ~/logs/2025-11-26/log.1
```

The script automatically uses the project's virtual environment Python, so you don't need to activate it manually.

The server will:
- Index the log file (takes ~1 second per 100MB)
- Start HTTP server on `http://localhost:5000`
- Automatically open your browser to the viewer

### 3. View Log

The browser will open automatically showing:
- Log file info (filename, total events, file size)
- Search bar
- Virtual scrolling viewport

Scroll through the log normally - the server streams only visible chunks, keeping browser memory constant.

## Usage

### Viewing Large Files

```bash
# View a 1GB log file
python3 logserver.py ~/logs/long-ride/log.1

# Use custom port
python3 logserver.py ~/logs/log.1 --port 8080

# Don't auto-open browser
python3 logserver.py ~/logs/log.1 --no-browser
```

### Searching

1. Type search term in search box
2. Press Enter or click "Search"
3. Use Prev/Next buttons to navigate matches

**Note:** In Phase 1, search loads all events to scan them. Phase 2 will add server-side streaming search for large files.

### Keyboard Shortcuts

- **Scroll wheel**: Navigate through log
- **Enter**: Start search
- **Mouse click Prev/Next**: Navigate search results

## Architecture

```
Browser                      Python Server                  Log File
┌──────────┐                ┌──────────────┐              ┌─────────┐
│ Virtual  │ GET /api/events│   Flask      │   Seek to   │ Binary  │
│ Scroller │───────────────>│   Server     │──offset────>│ Log     │
│          │ ?start=100     │              │             │         │
│          │ &end=300       │   Indexer    │   Parse     │ 1GB+    │
│          │                │   +          │   200       │ OK!     │
│ Renders  │<───────────────│   Decoder    │<──events────│         │
│ 200      │ JSON [{...}]   │              │             │         │
│ Events   │                └──────────────┘              └─────────┘
└──────────┘
```

**Key Points:**
- Server indexes file once on startup (maps event# → file offset)
- Browser requests only visible chunks (e.g., events 1000-1200)
- Server seeks to offset, parses chunk, returns JSON
- Browser RAM stays ~100MB regardless of file size
- LRU cache keeps recently accessed chunks for smooth scrolling

## File Versions

### For Desktop (Unlimited Size)
- **index_streaming.html** - This version, requires `logserver.py` running
- Handles GB+ files with constant memory
- No file size limit

### For Desktop (Small Files)
- **index.html** - Original virtual scrolling version (v2.1)
- Loads entire file into browser memory
- Good for files < 10MB

### For Mobile/Tablets
- **viewer_bundle.html** - Bundled version for file:// protocol (v2.1)
- Works offline on phones/tablets
- Recommended for files < 10MB

## Troubleshooting

### "No module named 'flask'"
Install dependencies:
```bash
pip install -r requirements_server.txt
```

### "No module named 'Logsyms'"
The server looks for Logsyms in `build/.venv/lib/python3.X/site-packages/`. Make sure you've built the umod4 project at least once:
```bash
cd ~/projects/umod4/build
ninja
```

### Browser shows "Error loading log"
Check terminal for server error messages. Common issues:
- Log file path incorrect
- File permissions
- Corrupted log file

### Slow scrolling
The server is indexing the file. Wait for "✅ Indexed" message before scrolling. Large files (GB+) may take several seconds to index.

## Performance

### Indexing Speed
- ~100MB/second on modern SSD
- 1GB file: ~10 seconds to index
- Index only built once when file opens

### Chunk Fetch Speed
- 200 events in < 10ms
- Smooth 60fps scrolling
- LRU cache eliminates re-fetching

### Memory Usage
- Browser: ~100MB constant (regardless of file size)
- Server: ~50MB + index (~8 bytes per event)
- Example: 1M events = ~8MB index

## Comparison: All Versions

| Version | Size Limit | Where | Load Time | RAM Usage | Offline |
|---------|-----------|-------|-----------|-----------|---------|
| **Streaming (v3.0)** | Unlimited | Desktop | Instant | 100MB | No (needs server) |
| **Virtual (v2.1)** | ~10MB | Desktop/Mobile | ~2 sec | 8GB/MB | Yes |
| **Bundle (v2.1)** | ~10MB | Mobile | ~2 sec | 8GB/MB | Yes |

## Future Features (Phase 2)

When WiFi upload is implemented on the Pico2W:

- **Auto-upload**: umod4 uploads logs via WiFi after ride
- **File watcher**: Server detects new logs automatically
- **Browser notifications**: "New ride log available!"
- **mDNS discovery**: Laptop advertises server for direct connection
- **Server-side search**: Search without loading all events

See [STREAMING_ARCHITECTURE.md](STREAMING_ARCHITECTURE.md) for full roadmap.

## Development

### Testing Indexer

```bash
python3 logindexer.py ~/logs/log.1
# Shows index stats and sample offsets
```

### Testing Server API

```bash
# In one terminal:
python3 logserver.py ~/logs/log.1

# In another terminal:
curl http://localhost:5000/api/info
curl 'http://localhost:5000/api/events?start=0&end=10'
```

### Adding New Event Types

1. Edit `logindexer.py` → `_get_event_data_length()`
2. Edit `logserver.py` → `LogChunkDecoder._decode_single_event()`
3. Follow pattern from existing event types

## Credits

Built for the umod4 motorcycle data logging system by Robin.

Uses:
- Flask (Python web server)
- Virtual scrolling (browser-side)
- Delta-based timestamp reconstruction

See main project: `/home/robin/projects/umod4/`
