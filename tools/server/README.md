# umod4 Server - Phase 0

Desktop application for receiving log files from umod4 motorcycle data loggers.

## Features

- **Multi-device support**: Track multiple umod4 devices (bikes, test benches)
- **Automatic log upload**: Devices automatically upload .um4 log files
- **Per-device configuration**: Custom display names and log storage paths
- **Transfer history**: View all uploads with speed, size, and status
- **Connection tracking**: Monitor device connections and online status
- **Viz integration**: Right-click log files to open in viz tool
- **Cross-platform**: Works on Windows, Linux, and macOS

## Installation

### Development (WSL2/Linux)

```bash
# Install dependencies
pip install -r requirements.txt

# Run server
python umod4_server.py
```

### Production (Windows .exe)

Build with Nuitka (see build_windows.bat) or download pre-built release.

## Usage

### Running the Server

```bash
# Default (port 8080, auto database location)
python umod4_server.py

# Custom port
python umod4_server.py --port 9000

# Custom database location
python umod4_server.py --db /path/to/devices.db
```

### Testing with Mock Device

```bash
# Simulate a device uploading 3 log files
python test_client.py

# Custom server and device
python test_client.py --server http://myserver:8080 --mac 28:cd:c1:0a:4b:2c --count 5
```

## Configuration

### First-Time Device Setup

When a new device connects:
1. Server creates default entry with MAC address as name
2. Default log storage path: `~/.umod4_server/logs/{mac-address}/`
3. Right-click device in GUI to rename and change storage path

### Device Configuration

Right-click device in device list:
- **Rename Device**: Set friendly name (e.g., "Tuono Red", "Test Bench")
- **Change Log Storage Path**: Select custom directory for logs
- **Open Log Folder**: Open log directory in file manager

### Transfer History

Right-click log file in transfer history:
- **Open in Viz Tool**: Launch visualizer with log file
- **Show in Folder**: Open containing folder

## Architecture

### Directory Structure

```
tools/server/
├── umod4_server.py         # Main application entry point
├── requirements.txt         # Python dependencies
├── http_server.py          # Flask HTTP server
├── test_client.py          # Mock device for testing
├── models/
│   ├── __init__.py
│   └── database.py         # SQLAlchemy database models
└── gui/
    ├── __init__.py
    └── main_window.py      # PySide6 main window and widgets
```

### Database Schema

**devices** table:
- mac_address (primary key)
- display_name
- log_storage_path
- wp_version, ep_version
- first_seen, last_seen
- auto_upload, notifications_enabled, firmware_track

**transfers** table:
- id (primary key)
- device_mac (foreign key)
- filename, size_bytes
- transfer_speed_mbps
- start_time, end_time
- status (success/failed/in_progress)
- error_message

**connections** table:
- id (primary key)
- device_mac (foreign key)
- connect_time, disconnect_time
- ip_address, duration_seconds

### HTTP API Endpoints

**Device Registration**
```
POST /api/device/register
Content-Type: application/json

{
  "mac_address": "28:cd:c1:0a:4b:2c",
  "wp_version": "1.0.0",
  "ep_version": "1.0.0",
  "ip_address": "192.168.1.150"
}
```

**List Uploaded Logs**
```
GET /logs/list/{device_mac}

Response: ["log_1.um4", "log_2.um4", ...]
```

**Upload Log File**
```
POST /logs/upload/{device_mac}
X-Filename: log_7.um4
Content-Type: application/octet-stream

[binary .um4 file data]
```

**Check Firmware Updates (stub)**
```
GET /firmware/latest/{device_mac}

Response: {"wp_version": "1.0.0", "update_available": false, ...}
```

## Default Locations

### Linux/WSL2
- Database: `~/.umod4_server/devices.db`
- Logs: `~/.umod4_server/logs/{device-mac}/`

### Windows
- Database: `%APPDATA%/umod4_server/devices.db`
- Logs: `%APPDATA%/umod4_server/logs/{device-mac}/`

## Development Status

Phase 0 implementation includes:
- ✅ SQLite database with multi-device support
- ✅ Flask HTTP server with device endpoints
- ✅ PySide6 GUI with device list
- ✅ Transfer history widget
- ✅ Right-click menu for viz integration
- ✅ Test client for simulation
- ⏳ Connection log widget (TODO)
- ⏳ Settings dialog (TODO)
- ⏳ System tray integration (TODO)
- ⏳ Nuitka build script (TODO)

## Next Steps (Phase 1+)

Phase 1 will add WP firmware WiFi functionality:
- SPI flash credential storage
- WiFi Access Point mode with captive portal
- Automatic connection to home network
- Log upload from WP to this server

See `~/.claude/plans/elegant-watching-grove.md` for full specification.
