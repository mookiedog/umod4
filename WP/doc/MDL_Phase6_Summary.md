# MDL Phase 6 Implementation Summary

**Phase:** Desktop Server Integration
**Status:** Implementation Complete - Ready for Testing
**Date:** 2026-01-10

---

## Overview

Phase 6 implements automatic log retrieval via a **pull-based architecture** where the desktop server automatically downloads log files from the umod4 device when it connects to WiFi.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  umod4 WP (Pico 2W)                         │
│                                                             │
│  WiFi connects → Send UDP check-in notification            │
│  {"device_mac":"xx:xx:xx:xx:xx:xx", "ip":"192.168.1.150"}  │
└──────────────────────┬──────────────────────────────────────┘
                       │ UDP port 8081
                       ↓
┌─────────────────────────────────────────────────────────────┐
│              Desktop Server (Python)                        │
│                                                             │
│  1. CheckInListener (UDP) receives notification            │
│  2. DeviceManager creates DeviceClient                      │
│  3. DeviceClient calls WP HTTP API:                         │
│     - GET /api/info (device info)                          │
│     - GET /api/list (list log files)                       │
│     - GET /logs/{filename} (download each new file)        │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Details

### WP Side (Pico Firmware)

#### 1. UDP Check-In Notification ([WiFiManager.cpp](../src/WiFiManager.cpp))

**New Methods:**
- `setServerAddress(server_ip, server_port)` - Configure server IP/port
- `sendCheckInNotification()` - Send UDP packet when WiFi connects

**Files Modified:**
- `WP/src/WiFiManager.h` - Added server address fields and methods
- `WP/src/WiFiManager.cpp` - Implemented UDP check-in notification
- `WP/src/main.cpp` - Added server address configuration

**Key Code:**
```cpp
void WiFiManager::sendCheckInNotification()
{
    if (!hasServerAddress_) return;

    // Build JSON payload
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_mac\":\"%s\",\"ip\":\"%s\"}",
             mac_str, ip_str);

    // Send UDP packet to server
    struct udp_pcb* pcb = udp_new();
    ip_addr_t server_addr;
    ip4addr_aton(serverIP_, &server_addr);

    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, strlen(payload), PBUF_RAM);
    memcpy(p->payload, payload, strlen(payload));

    udp_sendto(pcb, p, &server_addr, serverPort_);

    pbuf_free(p);
    udp_remove(pcb);
}
```

**Build-Time Configuration:**
```bash
# Set server IP address at build time (optional)
export SERVER_IP="192.168.1.100"  # Your desktop/laptop IP
export SERVER_PORT=8081            # Optional, defaults to 8081

# If not set, check-in is disabled (server must poll device manually)
```

#### 2. Existing API Endpoints (Already Implemented in Phases 1-2)

- `GET /api/info` - Returns device MAC, version, uptime, WiFi status
- `GET /api/list` - Returns JSON array of `.um4` files with sizes
- `GET /logs/{filename}` - Streams log file via custom filesystem bridge

### Server Side (Desktop Application)

#### 1. DeviceClient Module ([device_client.py](../../tools/server/device_client.py))

**Purpose:** HTTP client for communicating with WP device

**Key Methods:**
- `get_device_info()` → Dict with MAC, version, uptime
- `list_log_files()` → List of files with sizes
- `download_log_file(filename, destination)` → Downloads with progress callback
- `ping()` → Quick connectivity check

**Example Usage:**
```python
client = DeviceClient("192.168.1.150")
files = client.list_log_files()  # [{"filename": "ride_001.um4", "size": 5242880}, ...]
success, error = client.download_log_file("ride_001.um4", "/path/to/save.um4")
```

#### 2. CheckInListener Module ([checkin_listener.py](../../tools/server/checkin_listener.py))

**Purpose:** UDP listener for device check-in notifications

**Key Methods:**
- `start()` - Start UDP listener on port 8081
- `stop()` - Stop listener
- `set_callback(func)` - Register callback for check-in events

**Callback Signature:**
```python
def on_device_checkin(device_mac: str, device_ip: str):
    print(f"Device {device_mac} checked in from {device_ip}")
```

#### 3. DeviceManager Module ([device_manager.py](../../tools/server/device_manager.py))

**Purpose:** Orchestrates automatic log downloads

**Key Method:**
```python
def handle_device_checkin(device_mac: str, device_ip: str):
    # 1. Get or create device in database
    # 2. Record connection event
    # 3. Update device version info
    # 4. Download new log files
```

**Features:**
- Skips already-downloaded files (checks size)
- Creates transfer records in database
- Calculates transfer speed
- Progress callbacks for UI updates

#### 4. Integration ([umod4_server.py](../../tools/server/umod4_server.py))

**Modified Sections:**
- Added imports for `CheckInListener` and `DeviceManager`
- Initialize check-in listener on UDP port 8081
- Initialize device manager
- Wire up callbacks and Qt signals
- Auto-start listener when server starts

**Flow:**
1. UDP check-in received (background thread)
2. Emit Qt signal to main thread
3. Spawn download thread from main thread
4. Download logs in background
5. Update UI when complete

---

## Configuration

### WP Firmware Configuration

Edit your build script or set environment variables:

```bash
# ~/.bashrc or build script
export WIFI_SSID="YourHomeNetwork"
export WIFI_PASSWORD="your_wifi_password"
export SERVER_IP="192.168.1.100"     # IP of your desktop/laptop
export SERVER_PORT=8081               # Optional, defaults to 8081

# Then build
cd ~/projects/umod4/build
ninja
```

Alternatively, if `SERVER_IP` is not set, check-in is disabled. You can still manually download logs by opening `http://motorcycle.local/` in a browser.

### Server Configuration

No configuration needed - the server automatically:
- Listens on UDP port 8081 for check-ins
- Downloads to `~/.umod4_server/logs/{MAC_ADDRESS}/`
- Records transfers in SQLite database

---

## Testing

### Prerequisites

1. **WP firmware built with WiFi credentials:**
   ```bash
   export WIFI_SSID="YourNetwork"
   export WIFI_PASSWORD="password"
   export SERVER_IP="192.168.1.100"  # Your PC's IP
   ```

2. **Server dependencies installed:**
   ```bash
   cd ~/projects/umod4/tools/server
   pip install requests PySide6 Flask SQLAlchemy
   ```

### Test Procedure

#### Step 1: Start Server

```bash
cd ~/projects/umod4/tools/server
python umod4_server.py
```

**Expected Output:**
```
Initializing database...
Database: /home/user/.umod4_server/umod4.db
Initializing HTTP server on 0.0.0.0:8080
Initializing device manager...
Initializing check-in listener on UDP port 8081...
Starting HTTP server...
Starting check-in listener...
umod4 Server ready!
  - HTTP server (legacy push): http://0.0.0.0:8080
  - Check-in listener (pull): UDP port 8081
Waiting for device check-ins...
CheckInListener: Listening for device check-ins on UDP port 8081
```

#### Step 2: Power On WP (or Reset WiFi)

Plug WP into USB power in garage (or wherever it has WiFi access).

**Expected WP Serial Output:**
```
WiFiMgr: Power OK, enabling Station Mode
WiFiMgr: Connecting to SSID: YourNetwork
WiFiMgr: Link Up, waiting for IP...
WiFiMgr: Connected! IP: 192.168.1.150
WiFiMgr: Server address set to 192.168.1.100:8081
WiFiMgr: Sending check-in to 192.168.1.100:8081
WiFiMgr: Payload: {"device_mac":"28:cd:c1:0a:4b:2c","ip":"192.168.1.150"}
WiFiMgr: Check-in notification sent successfully
```

#### Step 3: Verify Server Receives Check-In

**Expected Server Output:**
```
CheckInListener: Device 28:cd:c1:0a:4b:2c checked in from 192.168.1.150
Device check-in: 28:cd:c1:0a:4b:2c at 192.168.1.150
DeviceManager: Handling check-in from 28:cd:c1:0a:4b:2c at 192.168.1.150
DeviceManager: Device info: {'device_mac': '28:cd:c1:0a:4b:2c', 'wp_version': '1.0.0', ...}
DeviceManager: Device has 3 log files
DeviceManager: Downloading ride_001.um4 (5242880 bytes)
Download started: ride_001.um4 from 28:cd:c1:0a:4b:2c
DeviceManager: Downloaded ride_001.um4 successfully (2.34 MB/s)
Download completed: ride_001.um4 from 28:cd:c1:0a:4b:2c (success=True)
...
```

#### Step 4: Verify Files Downloaded

```bash
ls -lh ~/.umod4_server/logs/28-cd-c1-0a-4b-2c/
```

**Expected Output:**
```
total 15M
-rw-r--r-- 1 user user 5.0M Jan 10 14:23 ride_001.um4
-rw-r--r-- 1 user user 7.0M Jan 10 14:23 ride_002.um4
-rw-r--r-- 1 user user 3.2M Jan 10 14:24 ride_003.um4
```

#### Step 5: Verify GUI Updates

- Main window should show the device in the device list
- Transfer history should show completed downloads
- Click "Refresh" to update if needed

---

## Troubleshooting

### Problem: No check-in received

**Symptoms:**
- Server shows "Waiting for device check-ins..." but nothing happens
- WP shows "WiFi connected" but no check-in message

**Possible Causes:**

1. **SERVER_IP not set at build time**
   - Check WP serial output for "Server address set to..." message
   - If missing, rebuild firmware with `export SERVER_IP="..."`

2. **Server IP changed**
   - WP uses IP from build time, server may have different IP now
   - Check server IP: `ip addr show` (Linux) or `ipconfig` (Windows)
   - Update SERVER_IP and rebuild

3. **Firewall blocking UDP port 8081**
   ```bash
   # Linux: Check firewall
   sudo ufw status
   sudo ufw allow 8081/udp

   # Test with netcat
   nc -ul 8081  # Listen on UDP 8081
   # Then power on WP, should see JSON payload
   ```

4. **Wrong network**
   - WP and server must be on same WiFi network
   - Check WP IP and server IP are in same subnet (e.g., 192.168.1.x)

### Problem: Check-in received but no downloads

**Symptoms:**
- Server logs "Device checked in" but no "Downloading..." messages

**Possible Causes:**

1. **WP HTTP server not responding**
   ```bash
   # Test manually from server machine
   curl http://192.168.1.150/api/info
   # Should return JSON with device info
   ```

2. **All files already downloaded**
   - DeviceManager skips files that exist with matching size
   - Check `~/.umod4_server/logs/{MAC}/` directory
   - Delete a file and trigger check-in again to test

3. **Firewall blocking HTTP**
   - WP HTTP server is on port 80
   - Ensure server can reach WP on port 80

### Problem: Downloads incomplete or corrupted

**Symptoms:**
- Files download but are wrong size
- Files fail to decode

**Solutions:**

1. **Check transfer history in GUI** - Look for "failed" status and error messages

2. **Verify file sizes match:**
   ```bash
   # On server
   curl http://192.168.1.150/api/list
   # Compare sizes to downloaded files
   ls -l ~/.umod4_server/logs/{MAC}/
   ```

3. **Check WiFi signal strength** - Weak signal causes packet loss
   - WP serial output shows WiFi RSSI in `/api/info`
   - Move WP closer to router for testing

4. **Delete and re-download:**
   ```bash
   rm ~/.umod4_server/logs/{MAC}/ride_001.um4
   # Trigger check-in again (power cycle WP or wait for reconnect)
   ```

---

## Future Enhancements

### Not Implemented (Deferred)

These features from the MDL Architecture doc are **not** implemented in Phase 6:

1. **Authentication** (Phase 5) - No auth tokens, all endpoints open
2. **File deletion** - Server can download but not delete files from WP
3. **SHA256 verification** - Files downloaded but hash not verified
4. **Resume downloads** - If download fails, must start over (no Range requests)
5. **Configuration via HTTP** - Can't change EPROM or settings via API yet

### Possible Future Work

- **mDNS support** - Use `motorcycle.local` instead of IP address
- **Automatic re-download** - Retry failed downloads automatically
- **Compression** - Compress logs before transfer (LZ4)
- **Selective download** - Only download logs from specific date range
- **Manual trigger** - GUI button to force check-in/download

---

## Summary

**What Works:**
✅ WP sends UDP check-in when WiFi connects
✅ Server receives check-in and auto-downloads logs
✅ Files downloaded to organized directories
✅ Transfer history recorded in database
✅ GUI shows devices and transfers
✅ Works with existing Phases 1-2 HTTP server

**What Doesn't Work:**
❌ Authentication (Phase 5 deferred)
❌ File deletion (not implemented)
❌ Hash verification (not implemented)
❌ Resume/Range requests (not implemented)

**Ready for Testing:** Yes - all core functionality implemented and ready to test on hardware.

---

## Files Created/Modified

### WP Firmware

**Modified:**
- `WP/src/WiFiManager.h` - Added server address config and UDP methods
- `WP/src/WiFiManager.cpp` - Implemented UDP check-in notification
- `WP/src/main.cpp` - Added server address initialization

**Build System:**
- No CMake changes needed - SERVER_IP is optional environment variable

### Desktop Server

**Created:**
- `tools/server/device_client.py` - HTTP client for pulling logs from WP
- `tools/server/checkin_listener.py` - UDP listener for check-in notifications
- `tools/server/device_manager.py` - Auto-download orchestration

**Modified:**
- `tools/server/umod4_server.py` - Integrated check-in listener and device manager

---

**Next Steps:** Flash updated WP firmware, start server, test end-to-end!
