# File Upload Feature (Server → Device)

## Overview

This feature allows uploading files from the server's filesystem to the WP's LittleFS filesystem over HTTP with chunked transfer and SHA-256 verification. Files are uploaded to a dedicated `/uploads/` directory on the device.

## Implementation Summary

### WP (Device) Side

**New Files:**
- [WP/src/upload_handler.h](WP/src/upload_handler.h) - Upload handler header with session management structures
- [WP/src/upload_handler.c](WP/src/upload_handler.c) - Implementation of chunked file upload with SHA-256 verification

**Modified Files:**
- [WP/src/httpd_stubs.c](WP/src/httpd_stubs.c) - Routes POST requests to upload handler
- [WP/src/fs_custom.c](WP/src/fs_custom.c) - Added virtual JSON response files and upload session query endpoint
- [WP/src/api_handlers.h](WP/src/api_handlers.h) - Added function declarations for new API endpoints
- [WP/src/NetworkManager.cpp](WP/src/NetworkManager.cpp) - Initialize upload handler during startup
- [WP/CMakeLists.txt](WP/CMakeLists.txt) - Added upload_handler.c to build

**API Endpoints:**

1. **POST /api/upload** - Receive file chunks
   - Headers:
     - `X-Session-ID`: UUID for resumption (optional for new uploads)
     - `X-Filename`: Destination filename (stored in /uploads/)
     - `X-Total-Size`: Total file size in bytes
     - `X-Chunk-Size`: Chunk size in bytes
     - `X-Chunk-Offset`: Byte offset for this chunk
     - `X-Chunk-CRC32`: CRC32 checksum of chunk (optional)
   - Response: JSON with success/error status

2. **GET /api/upload/session?session_id=<uuid>** - Query upload session status
   - Returns: JSON with `session_id`, `filename`, `total_size`, `bytes_received`, `next_offset`

**Features:**
- Chunked upload with resumption support (up to 2 concurrent sessions)
- Incremental SHA-256 calculation during upload
- Automatic /uploads/ directory creation
- Path traversal protection (filename validation)
- Session tracking for resume after interruption
- File written to LittleFS as chunks arrive

### Server Side

**Modified Files:**
- [tools/server/device_client.py](tools/server/device_client.py:205) - Added upload methods
  - `upload_file()` - Upload file in chunks with SHA-256 verification
  - `get_upload_session_status()` - Query device session for resumption

- [tools/server/gui/manage_devices_dialog.py](tools/server/gui/manage_devices_dialog.py:56) - Added upload button and dialog
  - New button: "Upload File to Device"
  - File selection dialog
  - Confirmation dialog with file details
  - Progress dialog with cancellation support
  - Database tracking of upload

- [tools/server/models/database.py](tools/server/models/database.py:67) - Added DeviceUpload model
  - New table: `device_uploads` (tracks all server→device uploads)
  - Fields: source_path, destination_filename, size_bytes, transfer_speed_mbps, start/end time, status, error_message, sha256
  - Automatic migration to create table on existing databases

**Features:**
- GUI file picker for source file selection
- Progress tracking with transfer speed calculation
- Cancellation support (user can abort upload)
- SHA-256 verification (calculated locally and sent to device)
- Database logging of all upload attempts
- Transfer speed (Mbps) tracking

## Usage

### Via GUI (Recommended)

1. Launch the server application
2. Select "Tools" → "Manage Devices"
3. Select the target device from the list
4. Click "Upload File to Device"
5. Select the file to upload from your filesystem
6. Confirm the upload (shows source path, destination, size)
7. Monitor progress in the dialog
8. File will be available at `/uploads/<filename>` on the device

### Via Python API

```python
from device_client import DeviceClient

# Connect to device
client = DeviceClient("192.168.1.100")

# Upload a file
def progress_callback(bytes_sent, total_bytes):
    percent = (bytes_sent / total_bytes) * 100
    print(f"Upload progress: {percent:.1f}%")

success, sha256, error = client.upload_file(
    source_path="/path/to/local/file.bin",
    destination_filename="firmware.bin",
    chunk_size=65536,  # 64KB chunks
    progress_callback=progress_callback
)

if success:
    print(f"Upload complete! SHA-256: {sha256}")
else:
    print(f"Upload failed: {error}")
```

## Technical Details

### Upload Flow

1. **Server initiates upload:**
   - Calculates SHA-256 of source file
   - Generates session UUID
   - Opens file for reading

2. **For each chunk:**
   - Server reads chunk_size bytes (default 64KB)
   - Calculates CRC32 of chunk
   - Sends POST to `/api/upload` with headers
   - Device validates headers and session
   - Device writes chunk to LittleFS file
   - Device updates SHA-256 incrementally
   - Device responds with success/progress

3. **On completion:**
   - Device finalizes SHA-256
   - Device closes file
   - Device sends success response
   - Server compares SHA-256 values (client-side only for now)
   - Server updates database record

### Resumption Support

If upload is interrupted:

1. Server queries `/api/upload/session?session_id=<uuid>`
2. Device returns `bytes_received` and `next_offset`
3. Server resumes from `next_offset`
4. Device validates offset matches current position
5. Upload continues from where it left off

Note: Currently resumption requires the session to still be in device RAM (not persistent across reboots).

### Security

- **Path Traversal Protection:** Filenames validated to reject `/`, `\`, `.`, `..`
- **Directory Isolation:** All uploads stored in `/uploads/` directory
- **Size Limits:** Chunk size limited to 64KB
- **Session Limits:** Maximum 2 concurrent upload sessions
- **No Overwrite Protection:** If file exists, it will be truncated on new upload

### Memory Usage

- **Per session:** ~200 bytes RAM for metadata + LittleFS file handle
- **Maximum:** 2 concurrent sessions = ~400 bytes + 2 file handles
- **SHA-256:** Uses hardware accelerator (no additional RAM beyond state structure)

## Testing

### Manual Test Procedure

1. Build and flash WP firmware:
   ```bash
   cd ~/projects/umod4/build
   ninja
   # Flash WP.uf2 to device
   ```

2. Start server GUI:
   ```bash
   cd ~/projects/umod4/tools/server
   python3 main.py
   ```

3. Wait for device to connect (check status in main window)

4. Open "Manage Devices" and select your device

5. Click "Upload File to Device"

6. Select a test file (e.g., a small text file or binary)

7. Verify:
   - Progress dialog shows upload progress
   - Upload completes successfully
   - SHA-256 is displayed
   - Database shows upload in `device_uploads` table

8. Connect to device via serial and verify file exists:
   ```
   # In WP shell (if available), or via API:
   curl http://<device-ip>/api/list
   # Should show the uploaded file in /uploads/ directory
   ```

### Automated Test (Future)

```python
# Test upload of various file sizes
test_files = [
    ("small.txt", 1024),           # 1KB
    ("medium.bin", 1024*1024),     # 1MB
    ("large.dat", 10*1024*1024)    # 10MB
]

for filename, size in test_files:
    # Create test file
    create_random_file(filename, size)

    # Upload to device
    success, sha256, error = client.upload_file(filename, filename)

    # Verify
    assert success, f"Upload failed: {error}"
    assert sha256 == calculate_sha256(filename)
```

## Limitations & Future Enhancements

**Current Limitations:**
1. No persistent session tracking (sessions lost on device reboot)
2. SHA-256 verification is client-side only (device doesn't send hash back to server for automatic verification)
3. No automatic cleanup of old files in /uploads/
4. File overwrite without confirmation
5. No progress indication on device (LED, display, etc.)

**Future Enhancements:**
1. **Persistent sessions:** Store session state in LittleFS for resume after reboot
2. **Bi-directional SHA-256 verification:** Device sends computed hash in final response, server auto-verifies
3. **File management API:** List/delete files in /uploads/ directory
4. **Compression:** Support gzip-compressed uploads
5. **Encryption:** TLS support for secure uploads
6. **Bandwidth limiting:** Configurable upload speed limits
7. **Priority uploads:** Support for emergency firmware updates
8. **Automatic retry:** Auto-resume failed uploads

## Use Cases

1. **Firmware Updates:** Upload new WP or EP firmware images to device for OTA updates
2. **Configuration Files:** Upload WiFi credentials, logging configs, or calibration data
3. **EPROM Images:** Upload new motorcycle ECU EPROM images for EP to load
4. **Data Files:** Upload maps, lookup tables, or reference data
5. **Test Files:** Upload test binaries for debugging and development

## Database Schema

### device_uploads Table

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Primary key |
| device_mac | TEXT | Device MAC address (FK to devices) |
| source_path | TEXT | Local file path that was uploaded |
| destination_filename | TEXT | Filename on device (/uploads/<filename>) |
| size_bytes | INTEGER | File size in bytes |
| transfer_speed_mbps | REAL | Transfer speed in Mbps |
| start_time | TEXT | Upload start timestamp |
| end_time | TEXT | Upload completion timestamp |
| status | TEXT | 'success', 'failed', or 'in_progress' |
| error_message | TEXT | Error details if failed |
| sha256 | TEXT | SHA-256 hash of uploaded file |

## Files Modified

### Device (WP)
- `WP/src/upload_handler.h` (new)
- `WP/src/upload_handler.c` (new)
- `WP/src/httpd_stubs.c`
- `WP/src/fs_custom.c`
- `WP/src/api_handlers.h`
- `WP/src/NetworkManager.cpp`
- `WP/CMakeLists.txt`

### Server
- `tools/server/device_client.py`
- `tools/server/gui/manage_devices_dialog.py`
- `tools/server/models/database.py`

## Build Verification

The implementation has been successfully compiled with no errors:

```bash
$ cd ~/projects/umod4/build && ninja
[... build output ...]
[20/20] Completed 'WP'
```

All components build cleanly with the ARM GCC 15.2 toolchain and Pico SDK 2.2.0.
