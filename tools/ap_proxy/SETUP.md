# ap_proxy Setup (WSL2)

The ap_proxy is a Pico W that bridges USB serial (WSL2) to the umod4 WiFi AP.
All communication goes over the single USB cable connecting the Pico W to the PC.

The ap_proxy's USB VID:PID (currently `1209:0001`) is defined in one place, `tools/ap_proxy/usb_ids.txt`.
Every command below that names a device uses that ID, so it does not matter which USB port the Pico W is plugged into.

---

## One-Time Windows Setup

1. Plug the Pico W into a USB port.

2. In a WSL terminal, run the binding script:

   ```bash
   tools/setup_usb_wsl
   ```

   A UAC permission prompt appears on the Windows desktop: click Yes.
   The script binds the ap_proxy and the debug probe so that WSL can use them.
   Binding survives unplug/replug and reboots.

3. Check the result (PowerShell):

   ```powershell
   usbipd list
   ```

   The ap_proxy shows up as `1209:0001  USB Serial Device`, with the state `Shared` (or `Attached` if WSL already has it).
   It may show `Reset` in the description. This is harmless.

To bind just the ap_proxy by hand instead, use an Administrator PowerShell:

```powershell
usbipd bind --hardware-id 1209:0001
```

---

## One-Time WSL2 Setup

The ap_proxy appears as a serial port (`/dev/ttyACM*`) that belongs to the `dialout` group, so your user must be a member of that group.
[BUILDING.md](../../BUILDING.md#install-openocd) shows how to check, and how to add yourself.

An earlier version of these instructions created a udev rule, `/etc/udev/rules.d/99-proxy-pico.rules`.
It is not needed: if you have it, you can delete it.

---

## Per-Session: Attach to WSL2

The test harness and `tools/flash_ap_proxy` attach the ap_proxy to WSL2 for you.
You only need to do it by hand when you use the ap_proxy outside of them, for example to run the smoke test.
After each WSL restart, or after replugging the Pico W (PowerShell, non-admin is fine once bound):

```powershell
usbipd attach --wsl --hardware-id 1209:0001
```

Verify it appeared in WSL:

```bash
ls /dev/ttyACM*
```

**If the device is stuck in "Reset" state and won't attach:** unplug it, plug it into a different USB port, and attach again.

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

* Sends the `BOOTSEL` command over serial (no button press needed if the firmware is running)
* Waits for the RP2 Boot device and attaches it to WSL2 via usbipd
* Flashes with picotool and reboots
* Reattaches the serial device to WSL2

If the Pico W has no ap_proxy firmware yet, hold BOOTSEL while plugging it in, run the script, and press Enter when it asks.

After flashing, verify:

```bash
ls /dev/ttyACM*
```

---

## Running the Smoke Test

```bash
uv run tools/ap_proxy/smoke_test.py /dev/ttyACM0
```

With a umod4 powered on in AP mode, the test will:

1. PING the ap_proxy
2. Check WiFi STATUS
3. SCAN for umod4_XXXX networks
4. CONNECT to the first one found (password = SSID)
5. GET /api/info
6. DISCONNECT

No credentials are written to the umod4.
The test can be run repeatedly.

---

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `usbipd attach` gives "Device in error state" | Unplug, plug into a different USB port, and attach again |
| `/dev/ttyACM*` permission denied | Your user is not in the `dialout` group, or the terminal was started before you joined it: see One-Time WSL2 Setup above |
| `/dev/ttyACM*` not found after flash | Run `usbipd attach --wsl --hardware-id 1209:0001` again |
| SCAN finds 0 networks | The umod4 is not powered on, or not in AP mode |
| CONNECT times out | Wrong password: the default is the SSID itself (for example `umod4_1EA3`) |
