# ap_proxy Setup (WSL2)

The ap_proxy is a Pico W that bridges USB serial (WSL2) to the umod4 WiFi AP.
All communication goes over the single USB cable connecting the Pico W to the PC.

---

## One-Time Windows Setup

1. Plug the Pico W into a USB port.

2. Find its busid (Admin PowerShell):
   ```powershell
   usbipd list
   ```
   Look for `2e8a:000a  USB Serial Device`. Note the busid (e.g. `5-3`).
   The device may show `Reset` in the description — this is harmless.

3. Bind it (Admin PowerShell, one-time per machine):
   ```powershell
   usbipd bind --busid=5-3
   ```

---

## One-Time WSL2 Setup

Create a udev rule so the device gets world-readable permissions automatically on every enumeration:

```bash
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", MODE="0666"' | \
    sudo tee /etc/udev/rules.d/99-proxy-pico.rules
sudo udevadm control --reload-rules
```

---

## Per-Session: Attach to WSL2

After each WSL restart, or after replugging the Pico W (PowerShell, non-admin is fine once bound):

```powershell
usbipd attach --wsl --busid=5-3
```

Verify it appeared in WSL:
```bash
ls /dev/ttyACM*
```

**If the device is stuck in "Reset" state and won't attach:** unplug, plug into a different USB port, run `usbipd list` to get the new busid, bind it, then attach.

---

## Building

From VS Code: **F7**

Or from the command line:
```bash
cd ~/projects/umod4/build && ninja
```

Output: `build/ap_proxy/ap_proxy.elf` and `ap_proxy.uf2`

---

## Flashing

```bash
tools/flash_ap_proxy
```

This script handles everything automatically:
- Sends `BOOTSEL` command over serial (no button press needed if firmware is running)
- Waits for RP2 Boot device, attaches it to WSL2 via usbipd
- Flashes with picotool and reboots
- Reattaches the serial device to WSL2

After flashing, verify:
```bash
ls /dev/ttyACM*
```

---

## Running the Smoke Test

```bash
build/.venv/bin/python3 tools/ap_proxy/smoke_test.py /dev/ttyACM0
```

With a umod4 powered on in AP mode, the test will:
1. PING the ap_proxy
2. Check WiFi STATUS
3. SCAN for umod4_XXXX networks
4. CONNECT to the first one found (password = SSID)
5. GET /api/info
6. DISCONNECT

No credentials are written to the umod4. The test can be run repeatedly.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `usbipd attach` → "Device in error state" | Unplug, plug into a different USB port, rebind |
| `/dev/ttyACM0` permission denied | udev rule not installed — see One-Time WSL2 Setup above |
| `/dev/ttyACM*` not found after flash | Run `usbipd attach --wsl --busid=<id>` again |
| SCAN finds 0 networks | umod4 not powered on, or not in AP mode |
| CONNECT times out | Wrong password — default is the SSID itself (e.g. `umod4_1EA3`) |
