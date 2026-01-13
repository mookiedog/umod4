# Windows Firewall Configuration for umod4 Server

## Problem

The umod4 server runs in WSL2 (Linux) but needs to receive UDP packets from devices on your local network (WiFi). Windows Firewall blocks incoming UDP traffic by default, preventing the WP device check-in notifications from reaching the server.

## Solution

Add a Windows Firewall rule to allow incoming UDP traffic on port 8081.

---

## Method 1: PowerShell (Recommended)

### Step 1: Open PowerShell as Administrator

1. Click the **Start** menu
2. Type `powershell`
3. **Right-click** on "Windows PowerShell"
4. Select **"Run as administrator"**
5. Click **"Yes"** on the UAC prompt

### Step 2: Add Firewall Rule

Copy and paste this command into PowerShell:

```powershell
New-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" -Direction Inbound -Protocol UDP -LocalPort 8081 -Action Allow
```

### Expected Output

```
Name                  : {GUID}
DisplayName           : umod4 Server UDP Check-in
Description           :
DisplayGroup          :
Group                 :
Enabled               : True
Profile               : Any
Platform              : {}
Direction             : Inbound
Action                : Allow
EdgeTraversalPolicy   : Block
LooseSourceMapping    : False
LocalOnlyMapping      : False
Owner                 :
PrimaryStatus         : OK
Status                : The rule was parsed successfully from the store.
EnforcementStatus     : NotApplicable
PolicyStoreSource     : PersistentStore
PolicyStoreSourceType : Local
```

### Step 3: Verify Rule Was Created

```powershell
Get-NetFirewallRule -DisplayName "umod4 Server UDP Check-in"
```

You should see the rule listed.

---

## Method 2: Windows Defender Firewall GUI

### Step 1: Open Windows Defender Firewall

1. Press `Windows + R`
2. Type `wf.msc`
3. Press **Enter**

### Step 2: Create Inbound Rule

1. Click **"Inbound Rules"** in the left panel
2. Click **"New Rule..."** in the right panel (Actions)
3. Select **"Port"** → Click **Next**
4. Select **"UDP"**
5. Select **"Specific local ports"**
6. Type `8081`
7. Click **Next**
8. Select **"Allow the connection"**
9. Click **Next**
10. Check all three boxes: **Domain**, **Private**, **Public**
11. Click **Next**
12. Name: `umod4 Server UDP Check-in`
13. Description: `Allows UDP check-in notifications from umod4 devices`
14. Click **Finish**

---

## Verification

### Test 1: Check if Port is Listening

In WSL2, verify the server is listening:

```bash
ss -ulnp | grep 8081
```

Expected output:
```
UNCONN 0 0 0.0.0.0:8081 0.0.0.0:* users:(("python3",pid=12345,fd=5))
```

### Test 2: Power Cycle WP Device

1. Restart the umod4 server:
   ```bash
   ~/projects/umod4/tools/server/run_server.sh
   ```

2. Power cycle the WP device (unplug and replug USB)

3. Watch WP serial output - should show:
   ```
   WiFiMgr: Connected! IP: 192.168.1.252
   WiFiMgr: Sending check-in to 192.168.1.198:8081
   WiFiMgr: Check-in notification sent successfully
   ```

4. Watch server terminal - should now show:
   ```
   CheckInListener: Device 2c:cf:67:d2:1e:9b checked in from 192.168.1.252
   Device check-in: 2c:cf:67:d2:1e:9b at 192.168.1.252
   DeviceManager: Handling check-in from 2c:cf:67:d2:1e:9b at 192.168.1.252
   ```

5. Check server GUI - device should appear in device list

---

## Troubleshooting

### Rule exists but still no packets

1. **Check rule is enabled:**
   ```powershell
   Get-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" | Select-Object Enabled
   ```
   Should show `Enabled : True`

2. **Verify rule applies to Private networks:**
   ```powershell
   Get-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" | Get-NetFirewallProfile
   ```
   Should include your active network profile (Domain/Private/Public)

3. **Check Windows network profile:**
   - Your network should be set to "Private" not "Public"
   - Go to Settings → Network & Internet → WiFi → Properties
   - Set "Network profile" to **Private**

### Remove rule (if needed)

To remove the rule and start over:

```powershell
Remove-NetFirewallRule -DisplayName "umod4 Server UDP Check-in"
```

---

## Why This Is Needed

- **WSL2 networking:** WSL2 uses a virtual network adapter that bridges to Windows
- **Windows is the gateway:** All network traffic goes through Windows first
- **Default deny:** Windows Firewall blocks incoming traffic by default
- **Localhost exception:** Traffic from 127.0.0.1 (localhost) is allowed, which is why the test packet from the same machine worked
- **Network traffic blocked:** Traffic from other devices on the network (like the WP at 192.168.1.252) requires an explicit firewall rule

---

## Security Notes

- This rule allows **any device** on your network to send UDP packets to port 8081
- The umod4 server only listens for check-in notifications (safe)
- For production use, consider restricting by IP address:
  ```powershell
  New-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" `
    -Direction Inbound -Protocol UDP -LocalPort 8081 -Action Allow `
    -RemoteAddress 192.168.1.0/24
  ```
  This restricts to your local subnet only.
