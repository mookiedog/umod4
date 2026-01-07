# Motorbike Data Link (MDL) Architecture Specification

**Version:** 1.0
**Last Updated:** 2026-01-07
**Status:** Design Phase

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architectural Decision Process](#2-architectural-decision-process)
3. [System Architecture](#3-system-architecture)
4. [Network Topology](#4-network-topology)
5. [Use Case Flows](#5-use-case-flows)
6. [Protocol Specification](#6-protocol-specification)
7. [Security and Authentication](#7-security-and-authentication)
8. [Implementation Details](#8-implementation-details)
9. [Trade-offs and Future Considerations](#9-trade-offs-and-future-considerations)
10. [References](#10-references)

---

## 1. Overview

### 1.1 Purpose

The Motorbike Data Link (MDL) system provides automated log retrieval, remote configuration, and Over-the-Air (OTA) firmware updates for the umod4 motorcycle datalogging system. The system enables seamless data transfer between the Pico 2W (RP2350) on the motorcycle and a PC-based server at home or a laptop at the track.

### 1.2 Goals

1. **Automatic log synchronization**: Log files upload automatically when the bike is powered and WiFi is available
2. **Dual-mode operation**: Support both WiFi (at home) and USB (at track) with identical functionality
3. **Simple user experience**: No custom drivers or complex setup required
4. **Reliable transfers**: Resume capability for interrupted uploads
5. **Secure operations**: Protect against unauthorized firmware updates and file deletion
6. **Direct access**: Standard web browser and command-line tools (curl, wget) work without custom software

### 1.3 System Components

```
┌──────────────────────────────────────────────────────────────┐
│                     umod4 System (Motorcycle)                │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Pico 2W (RP2350) - WP Processor                       │  │
│  │                                                        │  │
│  │  ┌─────────────────────────────────────────────────┐   │  │
│  │  │   lwIP HTTP Server (httpd)                      │   │  │
│  │  │   - REST API endpoints                          │   │  │
│  │  │   - Static file serving (web UI)                │   │  │
│  │  │   - Custom filesystem (SD card via LittleFS)    │   │  │
│  │  └─────────────────────────────────────────────────┘   │  │
│  │                      ↓                                 │  │
│  │  ┌─────────────────────────────────────────────────┐   │  │
│  │  │   lwIP Network Stack                            │   │  │
│  │  └─────────────────────────────────────────────────┘   │  │
│  │           ↓                    ↓                       │  │
│  │  ┌──────────────┐    ┌──────────────────────┐          │  │
│  │  │   netif_0    │    │      netif_1         │          │  │
│  │  │   (WiFi)     │    │   (USB-Ethernet)     │          │  │
│  │  │  CYW43439    │    │   TinyUSB CDC-ECM/   │          │  │
│  │  │              │    │   RNDIS              │          │  │
│  │  └──────────────┘    └──────────────────────┘          │  │
│  │                                                        │  │
│  │  ┌─────────────────────────────────────────────────┐   │  │
│  │  │   SD Card Storage (LittleFS)                    │   │  │
│  │  │   - Log files (*.um4)                           │   │  │
│  │  │   - Configuration                               │   │  │
│  │  └─────────────────────────────────────────────────┘   │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
                          ↓ WiFi or USB
┌──────────────────────────────────────────────────────────────┐
│              PC/Laptop (Home or Track)                       │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  umod4 Desktop Server (Python/Flask)                   │  │
│  │  - Device management                                   │  │
│  │  - Log storage and organization                        │  │
│  │  - Firmware update staging                             │  │
│  │  - GUI for monitoring                                  │  │
│  └────────────────────────────────────────────────────────┘  │
│                          OR                                  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Web Browser / curl                                    │  │
│  │  - Direct HTTP access to Pico                          │  │
│  │  - View web UI at http://motorcycle.local or           │  │
│  │    http://192.168.7.1 (USB)                            │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. Architectural Decision Process

### 2.1 The Core Question: Client or Server?

The fundamental architectural decision for MDL was whether the Pico 2W should act as:

**Option A: HTTP Client (Pull-based)**
- Pico initiates connections to a known server
- Pico pushes log files to server via POST requests
- Pico polls server for OTA updates and commands

**Option B: HTTP Server (Push/Pull hybrid)**
- Pico runs HTTP server, waits for connections
- PC/laptop connects to Pico to pull log files
- PC/laptop pushes firmware updates to Pico
- Pico can be accessed directly via web browser

### 2.2 Initial Analysis: Client Approach Advantages

The client approach initially appeared favorable because:

1. **NAT/Firewall friendly**: Outgoing connections work without port forwarding
2. **Power control**: Pico controls when it's ready to communicate
3. **Traditional pattern**: Common for IoT devices (Pico as peripheral, PC as controller)
4. **Automated uploads**: Pico can automatically push logs on schedule

### 2.3 The Critical Insight: USB-Ethernet

The decision shifted dramatically upon recognizing that **USB can be implemented as a network interface** rather than as a serial connection:

**TinyUSB provides CDC-ECM and RNDIS drivers** that make the Pico appear as an Ethernet adapter to the host computer. This means:

```
Traditional serial approach (what was initially considered):
┌──────────┐                    ┌──────────┐
│   Pico   │ ←─── USB-Serial ──→│  Laptop  │
└──────────┘      /dev/ttyACM0  └──────────┘
                  Custom framing protocol required
                  Different code path from WiFi

USB-Ethernet approach (the key insight):
┌──────────┐                    ┌──────────┐
│   Pico   │ ←─ USB-Ethernet ──→│  Laptop  │
└──────────┘      192.168.7.1   └──────────┘
                  Standard TCP/IP networking
                  IDENTICAL code path as WiFi!
```

**Impact**: With USB-Ethernet, the transport layer becomes **completely transparent**. The lwIP network stack handles both WiFi and USB-Ethernet as standard network interfaces. No custom serial framing protocol is needed. No transport abstraction layer is needed. The same HTTP server code works for both.

### 2.4 Decision: HTTP Server Architecture

Based on the USB-Ethernet insight, the HTTP server approach became clearly superior:

| Criterion | Client Approach | Server Approach | Winner |
|-----------|----------------|-----------------|---------|
| **WiFi at home** | Works (needs server running) | Works (PC connects to Pico) | Tie |
| **USB at track** | Complex (needs serial framing) | Simple (USB-Ethernet) | ✅ **Server** |
| **Code complexity** | Separate WiFi/USB paths | Unified network stack | ✅ **Server** |
| **User interface** | Needs separate GUI/CLI tools | Direct browser access | ✅ **Server** |
| **Discovery** | Pico must know server address | Standard mDNS (motorcycle.local) | ✅ **Server** |
| **Standard tools** | Custom client needed | curl/wget/browser work | ✅ **Server** |
| **Development/testing** | Need running server | Test with browser/curl directly | ✅ **Server** |
| **Automation** | Built-in (Pico pushes) | Needs scripts (PC pulls) | ⚠️ Client |

**Verdict**: The server approach is superior for 6 out of 7 criteria. The one advantage of the client approach (built-in automation) can be addressed by:
- Having Pico send a "check-in" notification when WiFi connects
- PC server wakes up and pulls logs from Pico
- Best of both worlds: automation + all the benefits of server architecture

### 2.5 Why Not Both?

The Pico could theoretically run both an HTTP server (for direct access) and an HTTP client (for automated uploads). However:

1. **Complexity**: Two HTTP stacks to maintain
2. **Memory**: Limited RAM on RP2350 (520KB total)
3. **Unnecessary**: The check-in notification achieves the same goal with less complexity

**Decision**: Implement HTTP server only, with optional check-in notification for automation.

---

## 3. System Architecture

### 3.1 Layered Architecture

```
┌──────────────────────────────────────────────────────────┐
│                Application Layer                         │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ Log File   │ │   OTA    │ │  Config  │ │   Web    │   │
│  │  Manager   │ │  Update  │ │  Manager │ │    UI    │   │
│  └────────────┘ └──────────┘ └──────────┘ └──────────┘   │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│              HTTP Server Layer (lwIP httpd)              │
│  - Request routing                                       │
│  - REST API endpoints                                    │
│  - Static file serving                                   │
│  - Custom filesystem integration                         │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│              Network Layer (lwIP TCP/IP)                 │
│  - TCP/IP stack                                          │
│  - Socket management                                     │
│  - Multi-homed networking (WiFi + USB)                   │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│                 Link Layer (netif)                       │
│  ┌──────────────────────┐  ┌──────────────────────────┐  │
│  │   netif_0 (WiFi)     │  │  netif_1 (USB-Ethernet)  │  │
│  │   - CYW43439 driver  │  │  - TinyUSB CDC-ECM/RNDIS │  │
│  │   - DHCP client      │  │  - Static IP assignment  │  │
│  └──────────────────────┘  └──────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│               Physical/Hardware Layer                    │
│  ┌──────────────────────┐  ┌──────────────────────────┐  │
│  │   WiFi Radio         │  │   USB Controller         │  │
│  │   (2.4 GHz)          │  │   (USB 2.0 HS)           │  │
│  └──────────────────────┘  └──────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 3.2 Multi-Homed Networking

The Pico 2W operates as a **multi-homed host** with two active network interfaces:

**netif_0 (WiFi)**:
- CYW43439 wireless chip
- DHCP client (obtains IP from router)
- Typical IP: 192.168.1.x or 10.0.0.x (depends on home network)
- mDNS responder: `motorcycle.local`

**netif_1 (USB-Ethernet)**:
- TinyUSB CDC-ECM (Linux/macOS) or RNDIS (Windows)
- Static IP configuration: `192.168.7.1/24`
- Host obtains IP via DHCP from Pico's built-in DHCP server
- Typical host IP: `192.168.7.2`

**Key property**: lwIP treats both interfaces identically. The HTTP server listens on both interfaces simultaneously. A web browser or curl command works the same way regardless of which interface is used.

### 3.3 Filesystem Integration

```
┌──────────────────────────────────────────────────────────┐
│              lwIP HTTP Server (httpd)                    │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│        Custom Filesystem Bridge (fs_open_custom)         │
│  Maps HTTP paths to LittleFS operations:                 │
│  - /logs/ride_001.um4  → lfs_open("/ride_001.um4")       │
│  - /index.html         → Static embedded file            │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│              LittleFS Filesystem                         │
│  - Wear leveling                                         │
│  - Power-fail safe                                       │
│  - Mounted on SD card via SDIO                           │
└──────────────────────────────────────────────────────────┘
```

The lwIP httpd has limited filesystem support by default (romfs only). To serve files from the SD card, we enable `LWIP_HTTPD_CUSTOM_FILESYSTEM` and implement:

- `fs_open_custom()`: Maps HTTP paths to `lfs_open()`
- `fs_read_custom()`: Streams file data via `lfs_read()`
- `fs_close_custom()`: Cleans up with `lfs_close()`

This allows the HTTP server to serve log files directly from the SD card without loading them into RAM.

---

## 4. Network Topology

### 4.1 At Home: WiFi Mode

```
                    ┌──────────────────┐
                    │   Home Router    │
                    │  192.168.1.1     │
                    └────────┬─────────┘
                             │
            ┌────────────────┼───────────────┐
            │                │               │
     ┌──────┴──────┐  ┌──────┴──────┐  ┌─────┴──────┐
     │   umod4     │  │  Home PC    │  │   Phone    │
     │  Pico 2W    │  │ umod4 Server│  │            │
     │192.168.1.150│  │192.168.1.100│  │192.168.1.51│
     └─────────────┘  └─────────────┘  └────────────┘
     motorcycle.local

Connection flow:
1. Bike plugged into USB power (garage)
2. WP detects USB power, enables WiFi
3. Connects to known SSID (stored in SPI flash)
4. Obtains IP via DHCP (e.g., 192.168.1.150)
5. Registers mDNS name: motorcycle.local
6. Sends UDP "check-in" packet to home server (optional)
7. Home server connects to http://motorcycle.local
8. Server pulls log files, checks for OTA updates
```

**Discovery Methods**:
1. **mDNS**: PC discovers Pico via `motorcycle.local` (requires Avahi/Bonjour)
2. **Check-in notification**: Pico sends UDP packet to pre-configured server IP
3. **Manual**: User types IP address (found via router admin page)

### 4.2 At Track: USB-Ethernet Mode

```
     ┌───────────────────────────────────────┐
     │         Laptop (Track)                │
     │                                       │
     │  USB Host Controller                  │
     │         │                             │
     │         │ USB cable                   │
     └─────────┼─────────────────────────────┘
               │
     ┌─────────┼─────────────────────────────┐
     │         │         umod4 Pico 2W       │
     │   TinyUSB CDC-ECM/RNDIS               │
     │   (USB-Ethernet Adapter)              │
     │                                       │
     │   Pico IP: 192.168.7.1                │
     │   Host IP: 192.168.7.2 (via DHCP)     │
     └───────────────────────────────────────┘

Connection flow:
1. Plug USB cable into laptop
2. Laptop enumerates USB device
3. Recognizes CDC-ECM (Linux/macOS) or RNDIS (Windows)
4. Creates network interface (usb0, en0, etc.)
5. Obtains IP via DHCP from Pico: 192.168.7.2
6. User opens browser to http://192.168.7.1
7. Same web UI and API as WiFi mode
```

**No drivers required** (all modern operating systems):
- **Windows 7-10**: Uses built-in RNDIS driver (enabled via Microsoft OS 2.0 descriptors)
- **Windows 11+**: Supports both RNDIS and CDC-NCM natively
- **Linux**: Uses built-in CDC-ECM driver (kernel module, available since 2.x kernels)
- **macOS**: Uses built-in CDC-ECM driver (native support since Mac OS X 10.4 Tiger)
  - Confirmed working on macOS 10.7+, including current macOS 14 Sonoma and later
  - Apple's semi-generic USB CDC drivers support: ACM, ECM, NCM, EEM, RNDIS (10.15+)
  - Appears as network interface (e.g., "USB 10/100 LAN" in Network Preferences)

---

## 5. Use Case Flows

### 5.1 Use Case 1: Upload Logs at Home

**Scenario**: Bike comes home after a ride, rider plugs into power in garage. Log files need to be automatically transferred to PC server.

```
┌──────────┐                                  ┌──────────────┐
│  Pico 2W │                                  │  Home Server │
└────┬─────┘                                  └──────┬───────┘
     │                                               │
     │ 1. Detect USB power                           │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ 2. Connect to WiFi (known SSID)               │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ 3. Obtain IP via DHCP (192.168.1.150)         │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ 4. Register mDNS: motorcycle.local            │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ 5. Send check-in notification (UDP)           │
     ├─────────────────────────────────────────────>>│
     │                                               │
     │                   6. HTTP GET /api/list       │
     │                   (query available logs)      │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ 7. Return JSON: ["ride_001.um4",              │
     │                  "ride_002.um4"]              │
     ├──────────────────────────────────────────────>│
     │                                               │
     │         8. HTTP GET /logs/ride_001.um4        │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ 9. Stream file data (chunked transfer)        │
     ├─────────────────────────────────────────────>>│
     │                                               │
     │        10. HTTP POST /api/confirm-upload      │
     │        {"file": "ride_001.um4",               │
     │         "sha256": "abc123..."}                │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ 11. Verify SHA256, respond 200 OK             │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ (Repeat for ride_002.um4...)                  │
     │                                               │
```

**Key Features**:
- Automatic: No user intervention required
- Resumable: Server tracks partial downloads, can resume if interrupted
- Verified: SHA256 checksum ensures data integrity

### 5.2 Use Case 2: File Deletion

**Scenario**: After logs are uploaded and verified, they can be deleted from the Pico to free space. Server asks permission; Pico confirms.

**Two modes**:

**Mode A: Automatic deletion after verified upload**
```
┌──────────┐                                  ┌──────────────┐
│  Pico 2W │                                  │  Home Server │
└────┬─────┘                                  └──────┬───────┘
     │ (Upload completed, SHA256 verified)           │
     │                                               │
     │   HTTP POST /api/delete                       │
     │   {"file": "ride_001.um4",                    │
     │    "confirmed": true}                         │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ Delete file, respond 200 OK                   │
     ├──────────────────────────────────────────────>│
```

**Mode B: Manual approval required**
```
┌──────────┐                                  ┌──────────────┐
│  Pico 2W │                                  │  Home Server │
└────┬─────┘                                  └──────┬───────┘
     │                                               │
     │   HTTP POST /api/delete-request               │
     │   {"file": "ride_001.um4"}                    │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ Mark file for deletion, respond 202 Accepted  │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ (Human reviews on server GUI)                 │
     │                                               │
     │   HTTP POST /api/delete                       │
     │   {"file": "ride_001.um4",                    │
     │    "confirmed": true,                         │
     │    "auth_token": "..."}                       │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ Verify token, delete file, respond 200 OK     │
     ├──────────────────────────────────────────────>│
```

**Configuration**: User sets policy in server GUI (automatic or manual approval).

### 5.3 Use Case 3: OTA Firmware Update

**Scenario**: New firmware is built, server pushes it to Pico for update.

```
┌──────────┐                                  ┌──────────────┐
│  Pico 2W │                                  │  Home Server │
└────┬─────┘                                  └──────┬───────┘
     │ (Idle, WiFi connected)                        │
     │                                               │
     │                                         (User builds new
     │                                          firmware, places
     │                                          in server OTA dir)
     │                                               │
     │   HTTP POST /api/ota-available                │
     │   {"version": "2.3.0",                        │
     │    "size": 524288,                            │
     │    "sha256": "def456...",                     │
     │    "signature": "..."}                        │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ Store notification, respond 202 Accepted      │
     │ (Show notification on screen/BLE)             │
     ├──────────────────────────────────────────────>│
     │                                               │
     │ (User confirms OTA via button/BLE)            │
     │                                               │
     │   HTTP GET /api/ota/download/2.3.0            │
     │<<─────────────────────────────────────────────┤
     │                                               │
     │ Stream firmware binary (chunked)              │
     ├─────────────────────────────────────────────>>│
     │                                               │
     │ Write to OTA partition on flash               │
     │ Verify SHA256 checksum                        │
     │ Verify signature (RSA/Ed25519)                │
     │                                               │
     │   HTTP POST /api/ota/confirm                  │
     │   {"version": "2.3.0",                        │
     │    "status": "ready"}                         │
     │>>─────────────────────────────────────────────┤
     │                                               │
     │ Set boot flag to new partition                │
     │ Reboot via watchdog_reboot()                  │
     │                                               │
```

**Security Requirements**:
- Firmware must be signed with private key (developer holds key)
- Pico verifies signature with embedded public key
- SHA256 checksum prevents corruption
- User confirmation required (via button press or BLE app)

**Fail-safe**: If new firmware fails to boot (watchdog timeout), bootloader reverts to previous firmware.

### 5.4 Use Case 4: Control Interface (Web UI)

**Scenario**: Rider wants to change EPROM image for next ride.

**At home (WiFi) or track (USB):**
```
┌─────────────┐                              ┌──────────┐
│   Browser   │                              │ Pico 2W  │
└──────┬──────┘                              └────┬─────┘
       │                                          │
       │ HTTP GET http://motorcycle.local/        │
       ├─────────────────────────────────────────>│
       │                                          │
       │ Return index.html (embedded in flash)    │
       │<<────────────────────────────────────────┤
       │                                          │
       │ (HTML loads, makes AJAX calls)           │
       │                                          │
       │ HTTP GET /api/config                     │
       ├─────────────────────────────────────────>│
       │                                          │
       │ Return JSON:                             │
       │ {"eprom": "549USA",                      │
       │  "logging": true,                        │
       │  "sd_free_space": 5242880,               │
       │  "uptime_seconds": 3600}                 │
       │<<────────────────────────────────────────┤
       │                                          │
       │ (User selects "RP58" from dropdown)      │
       │                                          │
       │ HTTP POST /api/config                    │
       │ {"eprom": "RP58"}                        │
       ├─────────────────────────────────────────>│
       │                                          │
       │ Update config, respond 200 OK            │
       │ {"status": "will_apply_on_next_boot"}    │
       │<<────────────────────────────────────────┤
       │                                          │
```

**Frontend Options**:
1. **Simple HTML + Vanilla JS**: Minimal, works on any browser
2. **HTMX**: Declarative, minimal JS, ~14KB
3. **Pico.css**: Classless CSS framework, ~10KB

**Total web UI size**: <50KB (fits in Pico flash alongside firmware)

### 5.5 Use Case 5: At the Track (USB Mode)

**Scenario**: Bike is at track, rider plugs laptop into Pico via USB cable. No WiFi available.

```
User workflow:
1. Plug USB cable from laptop to Pico 2W
2. Laptop recognizes USB-Ethernet device
   - Windows: "RNDIS/Ethernet Gadget" appears in Network Adapters
   - Linux: "usb0" interface appears (dmesg shows CDC-ECM)
   - macOS: "USB 10/100 LAN" appears in Network Preferences
3. Laptop obtains IP 192.168.7.2 via DHCP
4. Open browser to http://192.168.7.1
5. Same web UI as WiFi mode appears
6. User can:
   - Download log files from today's session
   - Change EPROM selection for next session
   - View system status (SD space, uptime)
7. Unplug USB cable when done

Alternative (command-line):
$ curl http://192.168.7.1/api/list
["session_1.um4", "session_2.um4", "session_3.um4"]

$ curl http://192.168.7.1/logs/session_1.um4 -o session_1.um4
$ curl http://192.168.7.1/logs/session_2.um4 -o session_2.um4
$ curl http://192.168.7.1/logs/session_3.um4 -o session_3.um4
```

**Key advantage**: Laptop does NOT need to run umod4 server software. Direct HTTP access with standard tools.

**Optional: Running desktop server at track**:
For users who prefer automated workflows (batch download, organized storage, GUI monitoring), the umod4 desktop server can be run on the track laptop:

1. **Nuitka-compiled executable**: Server is packaged as standalone `.exe` (Windows) or binary (Linux/macOS)
   - No Python installation required on track laptop
   - Single executable, self-contained
   - Build process defined in `tools/server/build_windows.bat` (similar to viz tool)
   - ~50-80 MB executable size (includes Python runtime + dependencies)

2. **Benefits of running server at track**:
   - Same GUI and workflow as home setup
   - Automatic log organization by device and date
   - Right-click to open logs in viz tool
   - Transfer history and statistics
   - Firmware update staging

3. **Workflow comparison**:

   **Without server (direct access)**:
   - Plug in USB → Open browser to `http://192.168.7.1` → Manually download each file
   - Simple, no software needed
   - Good for quick downloads, viewing config

   **With server running**:
   - Plug in USB → Server auto-detects Pico → Server pulls logs automatically
   - Logs organized in `~/track_logs/2026-01-07/` with device name
   - Server GUI shows transfer progress, speed, history
   - Good for serious track days with multiple sessions

---

## 6. Protocol Specification

### 6.1 REST API Endpoints

All endpoints return JSON (except file downloads which return binary).

#### 6.1.1 Device Information

**GET /api/info**

Returns device information.

Response:
```json
{
  "device_mac": "28:cd:c1:0a:4b:2c",
  "wp_version": "1.2.0",
  "ep_version": "1.1.5",
  "uptime_seconds": 3600,
  "sd_total_bytes": 16106127360,
  "sd_free_bytes": 5242880,
  "current_eprom": "549USA",
  "logging_active": true
}
```

#### 6.1.2 Configuration Management

**GET /api/config**

Returns current configuration.

Response:
```json
{
  "eprom": "549USA",
  "logging_enabled": true,
  "wifi_ssid": "HomeNetwork",
  "auto_delete_after_upload": false
}
```

**POST /api/config**

Update configuration. Changes take effect immediately for some settings, on next boot for others.

Request:
```json
{
  "eprom": "RP58",
  "logging_enabled": true
}
```

Response:
```json
{
  "status": "ok",
  "reboot_required": true,
  "message": "EPROM change will apply on next ignition-on event"
}
```

#### 6.1.3 Log File Management

**GET /api/list**

List log files on SD card.

Response:
```json
{
  "files": [
    {
      "filename": "ride_001.um4",
      "size_bytes": 5242880,
      "timestamp": "2026-01-07T10:30:00Z",
      "sha256": "abc123..."
    },
    {
      "filename": "ride_002.um4",
      "size_bytes": 7340032,
      "timestamp": "2026-01-07T14:15:00Z",
      "sha256": "def456..."
    }
  ],
  "active_log": "ride_003.um4"
}
```

**GET /logs/{filename}**

Download log file. Returns binary data with `Content-Type: application/octet-stream`.

Supports HTTP Range requests for resumable downloads:
```
GET /logs/ride_001.um4
Range: bytes=2097152-
```

Response headers:
```
HTTP/1.1 206 Partial Content
Content-Range: bytes 2097152-5242879/5242880
Content-Type: application/octet-stream
Content-Length: 3145728
```

**POST /api/delete**

Delete log file (requires authentication).

Request:
```json
{
  "filename": "ride_001.um4",
  "sha256": "abc123...",
  "auth_token": "secret-token"
}
```

Response:
```json
{
  "status": "deleted",
  "sd_free_bytes": 8388608
}
```

#### 6.1.4 OTA Firmware Updates

**GET /api/ota/check**

Check current firmware version.

Response:
```json
{
  "wp_version": "1.2.0",
  "wp_build_date": "2026-01-05",
  "ep_version": "1.1.5"
}
```

**POST /api/ota/stage**

Stage new firmware for installation (requires authentication).

Request:
```json
{
  "version": "2.3.0",
  "size_bytes": 524288,
  "sha256": "...",
  "signature": "...",
  "auth_token": "secret-token"
}
```

Response:
```json
{
  "status": "ready_to_receive",
  "upload_url": "/api/ota/upload"
}
```

**POST /api/ota/upload**

Upload firmware binary (chunked transfer supported).

Request: Binary data (Content-Type: application/octet-stream)

Response:
```json
{
  "status": "received",
  "bytes_received": 524288,
  "sha256_matches": true,
  "signature_valid": true,
  "ready_to_install": true
}
```

**POST /api/ota/install**

Install staged firmware (requires user confirmation).

Request:
```json
{
  "version": "2.3.0",
  "auth_token": "secret-token"
}
```

Response:
```json
{
  "status": "installing",
  "message": "Device will reboot in 5 seconds"
}
```

#### 6.1.5 Command Execution

**POST /api/commands/{command}**

Execute system command.

Supported commands:
- `reboot`: Reboot device
- `shutdown`: Power down (if on battery)
- `remount-sd`: Remount SD card

Request:
```json
{
  "auth_token": "secret-token"
}
```

Response:
```json
{
  "status": "executing",
  "command": "reboot"
}
```

### 6.2 HTTP Status Codes

| Code | Meaning | Usage |
|------|---------|-------|
| 200 | OK | Successful request |
| 202 | Accepted | Request accepted, processing asynchronously |
| 206 | Partial Content | Partial file download (Range request) |
| 400 | Bad Request | Invalid JSON or missing parameters |
| 401 | Unauthorized | Missing or invalid auth_token |
| 403 | Forbidden | Operation not allowed (e.g., delete active log) |
| 404 | Not Found | File or endpoint doesn't exist |
| 409 | Conflict | Conflicting state (e.g., OTA already in progress) |
| 500 | Internal Server Error | SD card error, out of memory, etc. |

### 6.3 Authentication

Most read-only operations do not require authentication. Write operations (delete, OTA, config changes) require `auth_token` header:

```
POST /api/delete
X-Auth-Token: your-secret-token-here
Content-Type: application/json

{"filename": "ride_001.um4"}
```

**Token storage**:
- Token stored in SPI flash (separate from WiFi credentials)
- Generated on first boot (random 128-bit value)
- Displayed on screen or via serial console
- Can be changed via BLE app (future)

**Security model**: Physical access = authorization. If you can plug into USB or connect to WiFi, you can get the token from the screen.

---

## 7. Security and Authentication

### 7.1 Threat Model

**In-scope threats**:
1. Malicious firmware update (bricking device)
2. Unauthorized file deletion (data loss)
3. Configuration tampering (wrong EPROM selected)

**Out-of-scope threats**:
1. Network-level attacks (WiFi password cracking) - rely on WPA2 security
2. Physical attacks (someone steals the bike) - out of band
3. Side-channel attacks - not applicable to this use case

### 7.2 Authentication Scheme

**Two-tier authentication**:

**Tier 1: Network access**
- WiFi: WPA2 password (user's home network)
- USB: Physical access (must plug in cable)

**Tier 2: API authentication**
- Read-only endpoints: No authentication required
- Write endpoints: Require `X-Auth-Token` header

**Token generation**:
```c
// On first boot (or after factory reset)
uint8_t token[16];
pico_unique_board_id_t board_id;
pico_get_unique_board_id(&board_id);

// Mix board ID with random seed
sha256_context ctx;
sha256_init(&ctx);
sha256_update(&ctx, board_id.id, sizeof(board_id.id));
sha256_update(&ctx, random_bytes, 16);
uint8_t hash[32];
sha256_final(&ctx, hash);

// Use first 16 bytes as token
memcpy(token, hash, 16);

// Display as hex string
printf("Auth Token: ");
for (int i = 0; i < 16; i++) {
    printf("%02x", token[i]);
}
printf("\n");
```

**Token display**:
- Shown on startup screen (first 5 seconds)
- Available via `/api/info` endpoint (returns token if accessed from localhost)
- Written to `auth_token.txt` on SD card (for backup)

### 7.3 Firmware Signature Verification

**Signing process** (on developer machine):
```bash
# Generate key pair (one-time, developer keeps private key secure)
$ openssl genpkey -algorithm ED25519 -out private.pem
$ openssl pkey -in private.pem -pubout -out public.pem

# Sign firmware binary
$ openssl dgst -sha256 -sign private.pem -out WP.sig WP.bin

# Embed public key in firmware at build time
$ xxd -i public.pem > public_key.c
```

**Verification process** (on Pico):
```c
// Public key embedded in firmware at compile time
extern const uint8_t PUBLIC_KEY[];
extern const size_t PUBLIC_KEY_LEN;

bool verify_firmware_signature(const uint8_t* firmware_data,
                                size_t firmware_size,
                                const uint8_t* signature,
                                size_t signature_len)
{
    // Calculate SHA256 of firmware
    uint8_t hash[32];
    sha256(firmware_data, firmware_size, hash);

    // Verify signature using Ed25519
    return ed25519_verify(signature, hash, 32, PUBLIC_KEY);
}
```

**Key management**:
- Private key: Stored securely on developer machine (NOT in repo)
- Public key: Embedded in Pico firmware (public, safe to distribute)
- Key rotation: Requires firmware update with new public key

### 7.4 File Deletion Protection

**Two-step deletion**:

1. **Request deletion**: Server sends delete request with file SHA256
2. **Verification**: Pico calculates SHA256 of file, compares to request
3. **Confirmation**: Pico deletes file only if SHA256 matches

This prevents accidental deletion of wrong file due to race condition (e.g., file renamed between list and delete).

```json
POST /api/delete
{
  "filename": "ride_001.um4",
  "sha256": "abc123..."  // Must match actual file
}
```

If SHA256 doesn't match:
```json
HTTP 409 Conflict
{
  "error": "sha256_mismatch",
  "message": "File has changed since list request",
  "expected": "abc123...",
  "actual": "xyz789..."
}
```

---

## 8. Implementation Details

### 8.1 lwIP Configuration

**Required lwipopts.h settings**:

```c
// HTTP server with custom filesystem
#define LWIP_HTTPD_CGI                  1   // Enable CGI handlers
#define LWIP_HTTPD_CUSTOM_FILESYSTEM    1   // Use custom filesystem (not romfs)
#define LWIP_HTTPD_SUPPORT_POST         1   // Enable POST requests
#define LWIP_HTTPD_MAX_REQUEST_URI_LEN  256 // Long filenames

// Multi-homed networking
#define LWIP_NETIF_HOSTNAME             1   // Set hostname for mDNS
#define LWIP_MDNS_RESPONDER             1   // Enable mDNS (motorcycle.local)

// Performance tuning
#define TCP_MSS                         1460  // Maximize throughput
#define TCP_SND_BUF                     (8 * TCP_MSS)  // Send buffer
#define TCP_WND                         (8 * TCP_MSS)  // Receive window
#define PBUF_POOL_SIZE                  32    // Packet buffer pool

// Memory allocation
#define MEM_SIZE                        (16 * 1024)  // lwIP heap size
#define MEMP_NUM_TCP_PCB                8            // Max TCP connections
#define MEMP_NUM_TCP_PCB_LISTEN         4            // Max listening sockets
```

### 8.2 TinyUSB USB-Ethernet Configuration

**Cross-platform strategy**: TinyUSB supports creating a composite device that presents both CDC-ECM and RNDIS interfaces simultaneously. The host OS automatically selects the appropriate interface:
- Windows 7-10: Uses RNDIS (enabled via Microsoft OS 2.0 descriptors)
- Windows 11+: Can use either RNDIS or CDC-NCM
- Linux/macOS: Uses CDC-ECM (preferred, more standard)

**Device descriptor** (usb_descriptors.c):

```c
// USB Configuration Descriptor
// Composite device with multiple network interfaces

// Interface 0: CDC-ECM (for Linux/macOS)
//   - Communication Interface
//   - Data Interface

// Interface 1: RNDIS (for Windows)
//   - Communication Interface
//   - Data Interface

// Microsoft OS 2.0 Descriptors (tells Windows to use RNDIS)
#define MS_OS_20_DESC_LEN  0xA2

TU_ATTR_PACKED_BEGIN
typedef struct TU_ATTR_PACKED {
  MS_OS_20_SET_HEADER_DESC header;
  MS_OS_20_COMPATIBLE_ID_DESC compatible_id;
  MS_OS_20_PROPERTY_DESC property;
} ms_os_20_desc_t;
TU_ATTR_PACKED_END

const ms_os_20_desc_t ms_os_20_desc = {
  // Set Header
  .header = {
    .wLength = sizeof(MS_OS_20_SET_HEADER_DESC),
    .wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
    .dwWindowsVersion = 0x06030000,  // Windows 8.1 or later
    .wTotalLength = sizeof(ms_os_20_desc_t)
  },
  // Compatible ID (tells Windows to use RNDIS driver)
  .compatible_id = {
    .wLength = sizeof(MS_OS_20_COMPATIBLE_ID_DESC),
    .wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
    .CompatibleID = "RNDIS",
    .SubCompatibleID = ""
  }
};
```

**Implementation note**: While a composite device is technically possible, TinyUSB examples typically use a single interface (either ECM or RNDIS) selected at compile time. For maximum compatibility with minimal complexity, we use **CDC-ECM as primary** since:
1. Linux and macOS prefer CDC-ECM (more standards-compliant)
2. Windows 11+ now supports CDC-NCM (related to ECM)
3. For Windows 7-10 compatibility, RNDIS mode can be selected via build flag if needed
4. Most users will have Windows 11+ or use Linux/macOS

**Network configuration** (ECM/RNDIS handler):

```c
// Called by TinyUSB when host requests network config
void tud_network_init_cb(void) {
  // Set Pico's MAC address
  static uint8_t mac[6] = {0x28, 0xcd, 0xc1, 0x0a, 0x4b, 0x2c};
  tud_network_link_state(true);
}

// DHCP server for USB interface
void dhcp_server_init(void) {
  // Offer 192.168.7.2 to host
  // Gateway: 192.168.7.1 (Pico)
  // DNS: 192.168.7.1 (Pico acts as DNS forwarder)
}
```

### 8.3 Custom Filesystem Bridge

**Integrating LittleFS with lwIP httpd**:

```c
#include "lwip/apps/fs.h"
#include "lfs.h"

// Custom filesystem state
struct fs_file {
  lfs_file_t* lfs_file;
  size_t len;
  size_t offset;
};

// Open file from SD card
int fs_open_custom(struct fs_file* file, const char* name) {
  // Strip leading slash
  if (name[0] == '/') name++;

  // Allocate LittleFS file handle
  lfs_file_t* lfs_file = malloc(sizeof(lfs_file_t));
  if (!lfs_file) return 0;

  // Open file on SD card
  int err = lfs_file_open(&lfs, lfs_file, name, LFS_O_RDONLY);
  if (err < 0) {
    free(lfs_file);
    return 0;
  }

  // Get file size
  lfs_soff_t size = lfs_file_size(&lfs, lfs_file);

  // Fill in fs_file structure
  file->lfs_file = lfs_file;
  file->len = size;
  file->offset = 0;

  return 1;  // Success
}

// Read data from file
int fs_read_custom(struct fs_file* file, char* buffer, int count) {
  lfs_file_t* lfs_file = file->lfs_file;
  lfs_ssize_t bytes_read = lfs_file_read(&lfs, lfs_file, buffer, count);
  if (bytes_read < 0) return -1;

  file->offset += bytes_read;
  return bytes_read;
}

// Close file
void fs_close_custom(struct fs_file* file) {
  lfs_file_t* lfs_file = file->lfs_file;
  lfs_file_close(&lfs, lfs_file);
  free(lfs_file);
}
```

### 8.4 WiFi State Machine

**Connection management**:

```c
typedef enum {
  WIFI_DISABLED,
  WIFI_SCANNING,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
} wifi_state_t;

void wifi_task(void) {
  static wifi_state_t state = WIFI_DISABLED;

  switch (state) {
    case WIFI_DISABLED:
      if (usb_power_detected() && !ignition_on()) {
        // Enable WiFi when on USB power (not riding)
        cyw43_arch_enable_sta_mode();
        state = WIFI_SCANNING;
      }
      break;

    case WIFI_SCANNING:
      // Try known SSIDs in priority order
      for (int i = 0; i < num_known_networks; i++) {
        if (try_connect(known_networks[i].ssid,
                        known_networks[i].password)) {
          state = WIFI_CONNECTING;
          break;
        }
      }
      break;

    case WIFI_CONNECTING:
      if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
        // Connected! Start HTTP server
        httpd_init();
        mdns_responder_init();
        send_checkin_notification();
        state = WIFI_CONNECTED;
      }
      break;

    case WIFI_CONNECTED:
      // Monitor connection, reconnect if lost
      if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
        state = WIFI_SCANNING;
      }
      break;
  }
}
```

### 8.5 Memory Management

**Total RP2350 RAM**: 520 KB (SRAM0-SRAM5 + SRAM8-SRAM9)

**Allocation**:
- FreeRTOS kernel: ~8 KB
- lwIP heap (MEM_SIZE): 16 KB
- lwIP packet buffers (PBUF_POOL): ~48 KB (32 buffers × 1536 bytes)
- TCP send/receive buffers: ~24 KB (8 × TCP_MSS × 2)
- HTTP server: ~8 KB
- LittleFS cache: ~64 KB (16 blocks × 4096 bytes)
- Application tasks: ~32 KB
- USB buffers: ~8 KB
- **Total**: ~208 KB used, **312 KB free** for application

**File streaming**: Files are streamed directly from SD card to network, never loaded into RAM. This allows serving multi-megabyte log files with minimal memory footprint.

---

## 9. Trade-offs and Future Considerations

### 9.1 Current Design Trade-offs

#### 9.1.1 HTTP Server vs HTTP Client

**Chosen**: HTTP Server

**Trade-off**: Loses built-in automation (Pico pushing logs automatically)

**Mitigation**: Check-in notification triggers server to pull logs

**Benefit**: Simpler architecture, works with standard tools, unified USB/WiFi handling

#### 9.1.2 USB-Ethernet vs USB-Serial

**Chosen**: USB-Ethernet (CDC-ECM/RNDIS)

**Trade-off**: Slightly higher memory usage (~8 KB for USB network stack vs ~2 KB for serial)

**Benefit**: No custom framing protocol needed, identical code path as WiFi, works with standard HTTP tools

#### 9.1.3 Web UI on Pico vs Separate GUI App

**Chosen**: Web UI served from Pico

**Trade-off**: Limited to static HTML/JS (no complex frameworks), consumes flash space (~50 KB)

**Benefit**: No separate app to install, works on any device with browser, same interface for WiFi and USB

#### 9.1.4 Authentication via Token vs Certificate-based

**Chosen**: Simple token (128-bit random value)

**Trade-off**: Less secure than TLS client certificates, token visible on screen

**Benefit**: Much simpler implementation, adequate for trusted home network, physical access = authorization model

#### 9.1.5 LittleFS vs FAT32

**Chosen**: LittleFS

**Trade-off**: Not natively readable by Windows/Mac (can't pop SD card into PC)

**Benefit**: Power-fail safe, wear leveling, better performance on large cards

**Note**: Users can download logs via HTTP instead of removing SD card

### 9.2 Future Enhancements

#### 9.2.1 Bluetooth LE Control Interface

**Goal**: Smartphone app for configuration and control

**Capabilities**:
- WiFi credential provisioning
- EPROM selection
- Real-time status monitoring
- File deletion (with physical proximity as authorization)

**Coexistence**: BLE and WiFi can run simultaneously on CYW43439 (time-multiplexed on same radio)

**Trade-off**: Adds complexity, but provides better UX for at-track configuration

#### 9.2.2 Compression for Log Uploads

**Current**: Raw binary transfer (uncompressed)

**Future**: Optional LZ4 compression on-the-fly

**Benefit**: Faster transfers, less bandwidth

**Trade-off**: CPU overhead, more complex implementation

**Analysis**: Log files are mostly numeric data (timestamps, sensor values), which compresses well (~30-40% size reduction with LZ4)

#### 9.2.3 TLS/HTTPS Support

**Current**: Plain HTTP

**Future**: Optional HTTPS (lwIP supports mbedTLS integration)

**Benefit**: Encrypted communication, prevents passive eavesdropping

**Trade-off**: Significantly higher memory usage (~80 KB for mbedTLS), slower performance, certificate management complexity

**Assessment**: Low priority for home network use case (WPA2 already encrypts WiFi traffic)

#### 9.2.4 Multiple File Upload/Download (Batch Operations)

**Current**: One file at a time via HTTP GET/POST

**Future**: Batch API for multiple files

Example:
```json
POST /api/batch-download
{
  "files": ["ride_001.um4", "ride_002.um4", "ride_003.um4"],
  "format": "zip"
}

Response: Streamed ZIP archive
```

**Benefit**: Faster for downloading multiple small files

**Trade-off**: Requires ZIP library (~20 KB), more complex error handling

#### 9.2.5 Remote Diagnostics

**Goal**: View real-time ECU data without physical access

**Capabilities**:
- Stream live sensor data (RPM, TPS, etc.)
- View GPS position
- Monitor system health (temperature, voltage)

**Protocol**: Server-Sent Events (SSE) or WebSocket

**Trade-off**: Requires bike to be powered and connected while riding (not typical use case)

**Use case**: Mainly useful for bench testing, not on-bike diagnostics

### 9.3 Known Limitations

1. **Single concurrent connection**: lwIP httpd handles one HTTP request at a time (MEMP_NUM_TCP_PCB=8 allows multiple connections, but httpd is single-threaded)

2. **No HTTP/2**: lwIP httpd is HTTP/1.1 only (HTTP/2 requires ALPN, too complex for embedded)

3. **Limited MIME type support**: Only basic types (text/html, application/json, application/octet-stream)

4. **No server push**: All communication is request-response (no push notifications to browser)

5. **File size limit**: Practical limit ~500 MB per file (due to LittleFS and transfer time)

6. **USB-Ethernet speed**: USB 2.0 High-Speed = 480 Mbps theoretical, ~40-60 MB/s practical (adequate for log files)

7. **WiFi interference**: 2.4 GHz only, no 5 GHz support (CYW43439 limitation)

### 9.4 Alternative Architectures Considered

#### 9.4.1 WebDAV Server

**Concept**: Expose SD card as network drive via WebDAV

**Pros**: Native OS integration (mount as drive), standard protocol

**Cons**: Complex protocol, larger memory footprint, limited lwIP WebDAV support

**Verdict**: REST API is simpler and sufficient for use case

#### 9.4.2 FTP Server

**Concept**: Classic FTP server for file transfers

**Pros**: Well-established protocol, good tool support (FileZilla, etc.)

**Cons**: Two ports (control + data), complex firewall traversal, no built-in encryption, ancient protocol

**Verdict**: HTTP is more modern and better supported

#### 9.4.3 SSH/SFTP

**Concept**: Secure shell for file transfer

**Pros**: Encrypted, standard protocol, scp/sftp tools available

**Cons**: Large memory footprint (~150 KB for libssh), CPU-intensive crypto

**Verdict**: Overkill for trusted home network, but could be future enhancement

#### 9.4.4 Custom Binary Protocol

**Concept**: Design proprietary protocol optimized for umod4

**Pros**: Can be highly optimized for specific use case

**Cons**: Requires custom client software, no standard tools, harder to debug

**Verdict**: HTTP's flexibility and tooling support outweigh optimization benefits

---

## 10. References

### 10.1 Technical Specifications

- **RP2350 Datasheet**: https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
- **CYW43439 Datasheet**: Infineon (under NDA)
- **lwIP Documentation**: https://www.nongnu.org/lwip/2_1_x/index.html
- **TinyUSB Documentation**: https://docs.tinyusb.org/
- **LittleFS Specification**: https://github.com/littlefs-project/littlefs/blob/master/SPEC.md

### 10.2 Standards and RFCs

- **HTTP/1.1**: RFC 9110 (https://www.rfc-editor.org/rfc/rfc9110.html)
- **REST API Design**: RFC 6570 (URI Templates)
- **USB CDC-ECM**: USB Class Definitions for Communication Devices 1.2
- **USB RNDIS**: Microsoft RNDIS Specification 1.0
- **mDNS**: RFC 6762 (https://www.rfc-editor.org/rfc/rfc6762.html)
- **SHA-256**: FIPS 180-4
- **Ed25519 Signatures**: RFC 8032 (https://www.rfc-editor.org/rfc/rfc8032.html)

### 10.3 Related umod4 Documentation

- [BUILDING.md](../../BUILDING.md) - Build system setup
- [CLAUDE.md](../../CLAUDE.md) - Project architecture and conventions
- [WP/README.md](../README.md) - WP firmware overview
- [tools/server/README.md](../../tools/server/README.md) - Desktop server documentation
- [tools/server/PHASE0_SUMMARY.md](../../tools/server/PHASE0_SUMMARY.md) - Server implementation status

### 10.4 Software Licenses

All components use permissive open-source licenses:

| Component | License | Notes |
|-----------|---------|-------|
| Pico SDK | BSD-3-Clause | Raspberry Pi Foundation |
| lwIP | BSD-3-Clause | Swedish Institute of Computer Science |
| TinyUSB | MIT | Ha Thach |
| LittleFS | BSD-3-Clause | ARM Limited |
| FreeRTOS | MIT | Amazon |
| CYW43439 Driver | Custom (Raspberry Pi) | Binary blob with open API |

---

## Appendix A: Comparison Matrix

### HTTP Client vs HTTP Server

| Aspect | Client (Push) | Server (Push/Pull) | Winner |
|--------|---------------|---------------------|--------|
| **Architecture** | | | |
| WiFi at home | Pico → Server (push) | Server → Pico (pull) | Tie |
| USB at track | Serial framing needed | USB-Ethernet (transparent) | **Server** |
| Code complexity | Two transport paths | One network stack | **Server** |
| Memory usage | Lower (~200 KB) | Slightly higher (~208 KB) | Client |
| **User Experience** | | | |
| Discovery | Pico knows server IP | mDNS (motorcycle.local) | **Server** |
| Direct access | Requires running server | Browser/curl direct | **Server** |
| Standard tools | Custom client needed | curl/wget/browser | **Server** |
| Web UI | Separate server app | Served from Pico | **Server** |
| **Operations** | | | |
| Log upload | Automatic (Pico pushes) | Manual/scripted (PC pulls) | Client |
| File deletion | Pico requests from server | PC requests from Pico | Tie |
| OTA updates | Pico polls server | Server pushes to Pico | Tie |
| Configuration | Pico reports to server | PC configures Pico | Tie |
| **Development** | | | |
| Testing | Need server running | Test with curl directly | **Server** |
| Debugging | Wireshark on server | Wireshark on Pico | Tie |
| Iteration speed | Build + deploy both | Build + deploy Pico only | **Server** |
| **Totals** | 1 win, 1 better | 7 wins, 0 better | **Server wins** |

**Decision**: Server architecture is superior due to USB-Ethernet simplification and unified codebase.

---

## Appendix B: Example Code Snippets

### B.1 HTTP Server Initialization

```c
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"

void mdl_init(void) {
  // Initialize lwIP network stack
  cyw43_arch_init();

  // Start WiFi in station mode (if USB powered)
  if (usb_power_detected()) {
    wifi_connect_to_known_network();
  }

  // Initialize USB-Ethernet (always active)
  tud_init(BOARD_TUD_RHPORT);
  dhcp_server_init();

  // Start HTTP server on both interfaces
  httpd_init();

  // Register mDNS responder
  mdns_resp_init();
  mdns_resp_add_netif(netif_default, "motorcycle");

  printf("MDL: HTTP server listening on:\n");
  printf("  WiFi: http://%s or http://motorcycle.local\n",
         ip4addr_ntoa(netif_ip4_addr(netif_wifi)));
  printf("  USB:  http://192.168.7.1\n");
}
```

### B.2 Custom Filesystem Bridge (Complete Example)

```c
// Custom filesystem implementation for lwIP httpd
// Maps HTTP paths to LittleFS files on SD card

#include "lwip/apps/fs.h"
#include "lfs.h"
#include <stdlib.h>
#include <string.h>

extern lfs_t lfs;  // Global LittleFS context

int fs_open_custom(struct fs_file* file, const char* name) {
  // Strip leading slash
  if (name[0] == '/') name++;

  // Special case: serve embedded files for web UI
  if (strcmp(name, "index.html") == 0) {
    extern const uint8_t index_html[];
    extern const size_t index_html_len;
    file->data = (const char*)index_html;
    file->len = index_html_len;
    file->index = 0;
    file->pextension = NULL;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return 1;
  }

  // Open file from SD card
  lfs_file_t* lfs_file = malloc(sizeof(lfs_file_t));
  if (!lfs_file) return 0;

  int err = lfs_file_open(&lfs, lfs_file, name, LFS_O_RDONLY);
  if (err < 0) {
    free(lfs_file);
    return 0;
  }

  lfs_soff_t size = lfs_file_size(&lfs, lfs_file);

  file->data = NULL;  // Streaming mode
  file->len = size;
  file->index = 0;
  file->pextension = lfs_file;  // Store LittleFS handle
  file->flags = 0;

  return 1;
}

int fs_read_custom(struct fs_file* file, char* buffer, int count) {
  if (!file->pextension) return -1;  // Not a LittleFS file

  lfs_file_t* lfs_file = (lfs_file_t*)file->pextension;
  lfs_ssize_t bytes_read = lfs_file_read(&lfs, lfs_file, buffer, count);

  if (bytes_read < 0) return -1;

  file->index += bytes_read;
  return bytes_read;
}

void fs_close_custom(struct fs_file* file) {
  if (file->pextension) {
    lfs_file_t* lfs_file = (lfs_file_t*)file->pextension;
    lfs_file_close(&lfs, lfs_file);
    free(lfs_file);
  }
}
```

### B.3 REST API Handler (CGI)

```c
// CGI handler for /api/list endpoint

#include "lwip/apps/httpd.h"
#include "lfs.h"
#include <stdio.h>

extern lfs_t lfs;

const char* cgi_list_handler(int iIndex, int iNumParams,
                              char* pcParam[], char* pcValue[]) {
  static char json_buffer[4096];
  char* ptr = json_buffer;
  ptr += sprintf(ptr, "{\"files\":[");

  // Scan SD card for .um4 files
  lfs_dir_t dir;
  if (lfs_dir_open(&lfs, &dir, "/") == 0) {
    struct lfs_info info;
    bool first = true;

    while (lfs_dir_read(&lfs, &dir, &info) > 0) {
      if (info.type != LFS_TYPE_REG) continue;

      const char* ext = strrchr(info.name, '.');
      if (!ext || strcmp(ext, ".um4") != 0) continue;

      if (!first) ptr += sprintf(ptr, ",");
      first = false;

      ptr += sprintf(ptr, "{\"filename\":\"%s\",\"size\":%u}",
                     info.name, info.size);
    }

    lfs_dir_close(&lfs, &dir);
  }

  ptr += sprintf(ptr, "]}");

  return "/api_list_response.json";  // Virtual file
}

// Register CGI handler
static const tCGI cgi_handlers[] = {
  {"/api/list", cgi_list_handler},
};

void register_api_handlers(void) {
  http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
}
```

---

## 11. Phased Implementation Plan

This section provides a practical roadmap for implementing the MDL system, broken into incremental phases that each deliver testable functionality.

### 11.1 Phase Prerequisites

**Existing WP Code Assessment**:
- ✅ **Keep**: `WiFiManager.cpp` - Already handles WiFi connection state machine
- ❌ **Replace**: `LogUploader.cpp`, `HttpClient.cpp` - Incompatible with HTTP server model
- ✅ **Keep**: Existing SD card, LittleFS, GPS, UART code - Core functionality unchanged

**Development Environment**:
- Pico SDK 2.2.0+ with lwIP support
- TinyUSB with CDC-ECM/RNDIS support
- Desktop server (Phase 0) already complete (`tools/server/`)
- Test devices: Laptop (Linux/Windows/macOS), USB cable, WiFi router

---

### 11.2 Phase 1: HTTP Server + WiFi Foundation

**Goal**: Serve a simple "Hello from umod4" web page at `http://motorcycle.local/` via existing WiFi

**Duration**: 2-3 days

**Rationale**: Start with WiFi since `WiFiManager.cpp` already exists. This allows testing the HTTP server immediately without implementing USB-Ethernet from scratch.

**Tasks**:

1. **Enable lwIP httpd** (`WP/src/lwipopts.h`):
   ```c
   #define LWIP_HTTPD                      1
   #define LWIP_HTTPD_CGI                  1
   #define LWIP_HTTPD_CUSTOM_FILESYSTEM    1
   #define LWIP_HTTPD_SUPPORT_POST         1
   #define LWIP_MDNS_RESPONDER             1
   ```

2. **Extend WiFiManager** (`WP/src/WiFiManager.cpp`):
   - Ensure `connectToKnownNetwork()` works reliably
   - Add `getNetif()` method to return lwIP netif pointer
   - Connection policy: Enable WiFi when USB powered AND ignition off

3. **Create minimal web UI** (`WP/www/index.html`):
   ```html
   <!DOCTYPE html>
   <html><head><title>umod4</title></head>
   <body>
     <h1>umod4 Data Logger</h1>
     <p>Status: Connected via WiFi</p>
     <p>Uptime: <span id="uptime">...</span></p>
     <script>
       fetch('/api/info')
         .then(r => r.json())
         .then(d => document.getElementById('uptime').textContent = d.uptime_seconds + 's');
     </script>
   </body></html>
   ```

4. **Initialize HTTP server** (`WP/src/NetworkManager.cpp` or new file):
   ```c
   // After WiFi connects
   httpd_init();
   mdns_resp_init();
   mdns_resp_add_netif(wifi_netif, "motorcycle");
   ```

5. **Implement first API endpoint** (`WP/src/api_handlers.c`):
   - `GET /api/info` → JSON with uptime, MAC, WiFi status, version

**Testing**:
```bash
# Ensure Pico connects to home WiFi
# Check router admin page for Pico IP, or:
$ ping motorcycle.local
PING motorcycle.local (192.168.1.150): 64 bytes...

$ curl http://motorcycle.local/
<!DOCTYPE html>...

$ curl http://motorcycle.local/api/info
{"device_mac":"28:cd:c1:0a:4b:2c","uptime_seconds":123,"wifi_ssid":"HomeNetwork"}

# Or open browser:
$ open http://motorcycle.local/  # macOS
$ xdg-open http://motorcycle.local/  # Linux
$ start http://motorcycle.local/  # Windows
```

**Acceptance Criteria**:
- ✅ WiFi connects automatically when USB powered
- ✅ mDNS works: `ping motorcycle.local` succeeds
- ✅ Browser shows web UI at `http://motorcycle.local/`
- ✅ `/api/info` returns valid JSON with WiFi status
- ✅ Total web UI size < 50KB

**Files Created/Modified**:
- `WP/www/index.html` (new)
- `WP/src/webui_data.c` (generated from HTML)
- `WP/src/api_handlers.c` (new)
- `WP/src/api_handlers.h` (new)
- `WP/src/NetworkManager.cpp` (new - HTTP server init)
- `WP/src/NetworkManager.h` (new)
- `WP/src/WiFiManager.cpp` (modify - add getNetif())
- `WP/src/lwipopts.h` (modify)

---

### 11.3 Phase 2: Custom Filesystem Bridge (SD Card Integration)

**Goal**: Serve log files from SD card via HTTP: `GET /logs/ride_001.um4`

**Duration**: 2-3 days

**Tasks**:

1. **Implement custom filesystem** (`WP/src/fs_custom.c`):
   - `fs_open_custom()`: Map HTTP path → `lfs_open()`
   - `fs_read_custom()`: Stream via `lfs_read()`
   - `fs_close_custom()`: Clean up with `lfs_close()`
   - Handle both embedded files (web UI) and SD files (logs)

2. **Register custom filesystem** (`WP/src/NetworkManager.cpp`):
   ```c
   #define LWIP_HTTPD_CUSTOM_FILESYSTEM 1
   // lwIP will call fs_open_custom(), fs_read_custom(), fs_close_custom()
   ```

3. **Implement `/api/list` endpoint** (CGI handler):
   - Scan SD card root for `*.um4` files
   - Return JSON array: `[{"filename":"ride_001.um4","size":5242880,"sha256":"..."}]`

4. **Streaming optimization**:
   - Use lwIP's `PBUF_POOL` for zero-copy streaming
   - Read directly from SD to TCP send buffer
   - Test with large files (50-100 MB)

**Testing**:
```bash
# List log files
$ curl http://192.168.7.1/api/list
{"files":[{"filename":"ride_001.um4","size":5242880},
          {"filename":"ride_002.um4","size":7340032}]}

# Download log file
$ curl http://192.168.7.1/logs/ride_001.um4 -o ride_001.um4
$ ls -lh ride_001.um4
-rw-r--r-- 1 user user 5.0M Jan  7 10:30 ride_001.um4

# Verify download speed
$ time curl http://192.168.7.1/logs/ride_001.um4 > /dev/null
# Should see ~5-10 MB/s over USB 2.0
```

**Acceptance Criteria**:
- ✅ `/api/list` returns all `.um4` files on SD card
- ✅ `GET /logs/{filename}` downloads file correctly
- ✅ Large files (100+ MB) download without errors
- ✅ Download speed ≥ 5 MB/s
- ✅ Memory usage stable (no leaks during repeated downloads)

**Files Created/Modified**:
- `WP/src/fs_custom.c` (new)
- `WP/src/fs_custom.h` (new)
- `WP/src/api_handlers.c` (modify - add `/api/list`)

---

### 11.4 Phase 3: USB-Ethernet Transport

**Goal**: Add USB-Ethernet so HTTP server is accessible via both USB **and** WiFi

**Duration**: 2-3 days

**Rationale**: Now that WiFi + HTTP work, add USB transport for track use. Both transports active simultaneously.

**Tasks**:

1. **Enable TinyUSB CDC-ECM** (`WP/CMakeLists.txt`, `WP/src/tusb_config.h`):
   ```cmake
   target_compile_definitions(WP PRIVATE
       CFG_TUD_ECM_RNDIS=1
       CFG_TUD_CDC=0  # Disable serial CDC if needed
   )
   ```

2. **Create USB descriptors** (`WP/src/usb_descriptors.c`):
   - CDC-ECM device descriptor (primary for Linux/macOS)
   - Microsoft OS 2.0 descriptors (for Windows RNDIS fallback)
   - Device strings ("umod4 Network Interface")

3. **Multi-homed networking** (`WP/src/NetworkManager.cpp`):
   - Initialize TinyUSB: `tud_init()`
   - Create `netif_1` (USB-Ethernet) alongside existing `netif_0` (WiFi)
   - Set static IP `192.168.7.1/24` for USB
   - Implement `tud_network_init_cb()`, `tud_network_recv_cb()`
   - HTTP server already listens on `0.0.0.0:80` (both interfaces)

4. **Simple DHCP server** (optional for USB):
   - Offer `192.168.7.2` to host
   - Or rely on host setting static IP

5. **mDNS on both interfaces**:
   ```c
   mdns_resp_add_netif(netif_usb, "motorcycle");  // Add USB to existing mDNS
   ```

**Testing**:
```bash
# Test 1: USB only (unplug from WiFi)
$ ping 192.168.7.1
PING 192.168.7.1: 64 bytes...

$ curl http://192.168.7.1/api/info
{"device_mac":"...","wifi_connected":false,"usb_connected":true}

# Test 2: USB + WiFi simultaneously
$ ping motorcycle.local  # Via WiFi
$ ping 192.168.7.1       # Via USB

# Test 3: Both interfaces work simultaneously
# Terminal 1 (USB):
$ time curl http://192.168.7.1/logs/ride_001.um4 -o usb.um4

# Terminal 2 (WiFi) - at same time:
$ time curl http://motorcycle.local/logs/ride_002.um4 -o wifi.um4

# Both downloads should succeed without interfering
```

**Acceptance Criteria**:
- ✅ Laptop recognizes Pico as network device (no driver install)
- ✅ HTTP server accessible via USB at `http://192.168.7.1`
- ✅ HTTP server accessible via WiFi at `http://motorcycle.local`
- ✅ Simultaneous downloads (USB + WiFi) work correctly
- ✅ Works on Windows, Linux, and macOS

**Files Created/Modified**:
- `WP/src/usb_descriptors.c` (new)
- `WP/src/NetworkManager.cpp` (modify - add USB netif)
- `WP/CMakeLists.txt` (modify)
- `WP/src/tusb_config.h` (modify or create)

---

### 11.5 Phase 4: Configuration and Control API

**Goal**: Web UI can change settings (EPROM selection, view status)

**Duration**: 2-3 days

**Tasks**:

1. **Implement configuration API** (`WP/src/api_handlers.c`):
   - `GET /api/config` → Current config JSON
   - `POST /api/config` → Update config
   - `POST /api/commands/reboot` → Reboot device

2. **Configuration storage** (simple version):
   - Store in `config.txt` on SD card (JSON format)
   - Load on boot, write on change
   - Future: Move to SPI flash for faster access

3. **Enhance web UI** (`WP/www/index.html`):
   - Add EPROM selection dropdown
   - Add "Apply" button → `POST /api/config`
   - Add system status display (SD space, GPS status)
   - Add reboot button

4. **Status reporting** (`GET /api/info` enhancement):
   ```json
   {
     "device_mac": "28:cd:c1:0a:4b:2c",
     "wp_version": "1.0.0",
     "ep_version": "1.0.0",
     "uptime_seconds": 3600,
     "sd_total_bytes": 16106127360,
     "sd_free_bytes": 5242880,
     "current_eprom": "549USA",
     "logging_active": true,
     "gps_locked": true,
     "wifi_connected": true,
     "wifi_ssid": "HomeNetwork",
     "wifi_rssi": -45
   }
   ```

**Testing**:
```bash
# Get current config
$ curl http://192.168.7.1/api/config
{"eprom":"549USA","logging_enabled":true}

# Change EPROM
$ curl -X POST http://192.168.7.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"eprom":"RP58"}'
{"status":"ok","reboot_required":true}

# Test web UI
$ open http://192.168.7.1/
# Select "RP58" from dropdown, click Apply
# Verify config change persists after reboot
```

**Acceptance Criteria**:
- ✅ Web UI shows system status (uptime, SD space, GPS, WiFi)
- ✅ User can change EPROM selection via web UI
- ✅ Config changes persist across reboot
- ✅ Reboot command works
- ✅ Invalid config rejected with error message

**Files Created/Modified**:
- `WP/src/api_handlers.c` (modify)
- `WP/src/ConfigManager.cpp` (new)
- `WP/src/ConfigManager.h` (new)
- `WP/www/index.html` (enhance)

---

### 11.6 Phase 5: Authentication and Security

**Goal**: Protect write operations with authentication token

**Duration**: 1-2 days

**Tasks**:

1. **Generate auth token** (`WP/src/main.cpp`, on first boot):
   - Mix `pico_unique_board_id()` + random seed
   - SHA256 hash → 128-bit token
   - Store in SPI flash
   - Display on boot screen (first 5 seconds)

2. **Implement token validation** (`WP/src/api_handlers.c`):
   - Check `X-Auth-Token` header on POST/DELETE requests
   - Return `401 Unauthorized` if missing/invalid
   - Allow GET requests without token

3. **Token display** (`GET /api/info` when accessed from localhost):
   ```json
   {
     "device_mac": "...",
     "auth_token": "a3f2...8d1c"  // Only if source IP is 192.168.7.2
   }
   ```

4. **Update web UI** (`WP/www/index.html`):
   - Fetch token from `/api/info`
   - Include `X-Auth-Token` header on config changes
   - Show error if token invalid

**Testing**:
```bash
# Without token - should fail
$ curl -X POST http://192.168.7.1/api/config \
  -d '{"eprom":"RP58"}'
HTTP/1.1 401 Unauthorized
{"error":"missing_auth_token"}

# With token - should succeed
$ TOKEN=$(curl http://192.168.7.1/api/info | jq -r .auth_token)
$ curl -X POST http://192.168.7.1/api/config \
  -H "X-Auth-Token: $TOKEN" \
  -d '{"eprom":"RP58"}'
HTTP/1.1 200 OK
```

**Acceptance Criteria**:
- ✅ Auth token generated and stored on first boot
- ✅ Token displayed on boot screen
- ✅ GET requests work without token
- ✅ POST/DELETE requests require valid token
- ✅ Web UI automatically includes token in requests

**Files Created/Modified**:
- `WP/src/AuthManager.cpp` (new)
- `WP/src/AuthManager.h` (new)
- `WP/src/api_handlers.c` (modify - add token checks)
- `WP/www/index.html` (modify)

---

### 11.7 Phase 6: Desktop Server Integration

**Goal**: Desktop server auto-detects Pico and pulls logs automatically

**Duration**: 1-2 days

**Tasks**:

1. **Check-in notification** (`WP/src/NetworkManager.cpp`):
   - When WiFi connects, send UDP packet to `<server_ip>:8081`
   - Payload: `{"device_mac":"...","ip":"192.168.1.150"}`
   - Server wakes up and connects to Pico

2. **Update desktop server** (`tools/server/http_server.py`):
   - Listen for UDP check-in packets
   - On check-in, connect to device and pull log list
   - Download any new log files
   - Update GUI with transfer progress

3. **Transfer protocol enhancements**:
   - Add `Range` header support for resume
   - Add SHA256 calculation endpoint: `GET /logs/{file}/sha256`
   - Server verifies file integrity

**Testing**:
```bash
# Start desktop server
$ cd tools/server
$ python umod4_server.py

# Plug in bike (or simulate WiFi connection)
# Server should show:
[INFO] Check-in from device 28:cd:c1:0a:4b:2c at 192.168.1.150
[INFO] Connecting to http://192.168.1.150
[INFO] Found 3 new log files
[INFO] Downloading ride_001.um4 (5.0 MB)...
[INFO] Download complete: 5.0 MB in 12.3s (416 KB/s)
```

**Acceptance Criteria**:
- ✅ Pico sends check-in notification on WiFi connect
- ✅ Desktop server auto-detects and connects
- ✅ Server downloads all new log files
- ✅ SHA256 verification succeeds
- ✅ Transfer history logged in database

**Files Created/Modified**:
- `WP/src/NetworkManager.cpp` (modify - add UDP check-in)
- `tools/server/http_server.py` (modify)

---

### 11.8 Phase 7: OTA Firmware Updates (Future)

**Goal**: Server can push firmware updates to Pico

**Duration**: 3-5 days

**Tasks** (high-level only, detailed design TBD):

1. Implement dual-partition boot loader
2. Add `/api/ota/stage` endpoint
3. Implement firmware signature verification (Ed25519)
4. Add `/api/ota/install` endpoint
5. Add rollback mechanism if new firmware fails

**Deferred**: This is a complex feature requiring bootloader work and cryptographic signing. Implement after core functionality is stable.

---

### 11.10 Testing and Validation

**Cross-platform testing matrix**:

| Feature | Windows 10 | Windows 11 | Linux (Ubuntu) | macOS (Sonoma) |
|---------|------------|------------|----------------|----------------|
| USB-Ethernet enumeration | ✅ | ✅ | ✅ | ✅ |
| HTTP access via USB | ✅ | ✅ | ✅ | ✅ |
| WiFi mDNS discovery | ✅ | ✅ | ✅ | ✅ |
| Large file download (100 MB) | ✅ | ✅ | ✅ | ✅ |
| Desktop server (Nuitka) | ✅ | ⏳ | ⏳ | ⏳ |

**Performance benchmarks**:
- USB download speed: ≥ 5 MB/s (target: 10 MB/s)
- WiFi download speed: ≥ 2 MB/s (depends on router)
- Memory usage: ≤ 300 KB total (leaves 220 KB for application)
- Simultaneous connections: ≥ 2 (USB + WiFi)

**Stress testing**:
- Download 10 files sequentially (check for memory leaks)
- Download 2 files simultaneously (USB + WiFi)
- Rapid connect/disconnect cycles (100x)
- Long-duration connection (24 hours idle)

---

### 11.11 Documentation and Deployment

**User documentation** (`WP/doc/MDL_User_Guide.md`):
- Quick start guide
- Troubleshooting common issues
- Network configuration tips

**Developer documentation** (`WP/doc/MDL_Development.md`):
- Building and flashing firmware
- Testing procedures
- Adding new API endpoints
- Debugging tips

**Release checklist**:
- [ ] All phases 1-7 complete and tested
- [ ] Cross-platform testing passed
- [ ] Performance benchmarks met
- [ ] User guide written
- [ ] Desktop server packaged with Nuitka
- [ ] GitHub release with binaries (.uf2, .exe)

---

### 11.12 Estimated Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| 1. HTTP Server + WiFi Foundation | 2-3 days | 3 days |
| 2. Filesystem Bridge (SD Card) | 2-3 days | 6 days |
| 3. USB-Ethernet Transport | 2-3 days | 9 days |
| 4. Config/Control API | 2-3 days | 12 days |
| 5. Authentication | 1-2 days | 14 days |
| 6. Server Integration | 1-2 days | 16 days |
| **Testing & Polish** | 3-5 days | **~3 weeks** |

**Revised ordering rationale**:
- Start with **WiFi first** (already in codebase via `WiFiManager.cpp`)
- Add HTTP server and web UI on top of working WiFi
- Then add filesystem bridge to serve log files
- Add USB-Ethernet as additional transport (more work than WiFi)
- Finally add config, auth, and server integration

**Notes**:
- Timeline assumes 1 developer working full-time
- Each phase is independently testable
- Phases can be merged/split based on progress
- OTA updates (Phase 7) deferred to post-MVP
- WiFi-first approach reduces risk by building on existing code

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-07 | Robin | Initial architecture specification based on design discussion |

---

**End of Document**
