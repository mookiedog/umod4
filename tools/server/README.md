# Umod4 Server

The umod4 server is a PC application that collects log files from umod4 devices over WiFi.
The server lets you:

* work with multiple umod4 devices
* manage logfiles on a device
* push OTA firmware updates to a device

The server is a PySide6 GUI backed by a small SQLite database.

## How it works

The server pulls logs from the device: it is the HTTP client, the device is the HTTP server.

1. When a device joins WiFi it sends a UDP check-in packet to port 54549.
2. The server's check-in listener (`checkin_listener.py`) receives it and hands the device's IP address to the device manager (`device_manager.py`).
3. The device manager uses `device_client.py` to list the logs on the device and download the ones it does not have yet.
4. Downloaded logs are stored per device, and the GUI shows the devices and their transfers.

If the server cannot open UDP port 54549 (for example because another copy is running, or a firewall blocks it), it says so at startup instead of running without hearing the devices.
The Windows firewall rule that lets check-ins through is described in [BUILDING.md](../../BUILDING.md).

## Running

The Python environment is managed by uv from the top of the repository (the `server` dependency group in `pyproject.toml`).
There is nothing to install by hand.

```bash
tools/server/run_server.sh
```

Options:

* `--db PATH`: use a different database file.
* `--flash-ep UF2_PATH --ip ADDRESS`: no GUI; upload an EP firmware file to the device at that address and reflash the EP.

## Where things are stored

| | Linux / WSL | Windows |
| --- | --- | --- |
| Database and settings | `~/.umod4_server/` | `%APPDATA%\umod4_server\` |
| Logs | `~/.umod4_server/logs/<device>/` | `%APPDATA%\umod4_server\logs\<device>\` |

The log folder for each device can be changed from the device's right-click menu in the GUI.

## Source layout

```text
tools/server/
├── umod4_server.py          # entry point
├── checkin_listener.py      # UDP check-in listener
├── device_manager.py        # reacts to check-ins, downloads new logs
├── device_client.py         # HTTP client for one device
├── connectivity_checker.py
├── timeout_monitor.py
├── models/                  # database and app settings
├── gui/                     # main window and dialogs
├── tests/
└── run_server.sh
```
