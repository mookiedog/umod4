# setup_usb_wsl.ps1
#
# One-time setup: run as Administrator in PowerShell on the Windows host.
#
# Binds all Raspberry Pi Pico-family USB devices so they can be shared with
# WSL2 via usbipd.  Binding is persistent -- devices stay bound across
# unplug/replug and reboots.
#
# Run this script whenever you add a new physical device.
# Devices must be plugged in when this script runs.
#
# PID reference: https://github.com/raspberrypi/usb-pid
#
# Usage:
#   From WSL (easiest):
#     tools/setup_usb_wsl
#
#   From Windows Explorer:
#     Right-click setup_usb_wsl.ps1 -> "Run as Administrator"
#
#   From an admin PowerShell on Windows:
#     Set-ExecutionPolicy Bypass -Scope Process; & "tools\setup_usb_wsl.ps1"

# Do not use Stop -- we handle errors per-command below.
$ErrorActionPreference = "Continue"

# Capture all output to a log file so the WSL launcher can display it.
$LogPath = "$env:TEMP\umod4_setup_usb_wsl.log"
Start-Transcript -Path $LogPath -Force | Out-Null

# Must run as administrator (checked here so transcript captures the error).
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator."
    Stop-Transcript | Out-Null
    exit 1
}

# Raspberry Pi VID 0x2E8A -- Pico family PIDs
# See https://github.com/raspberrypi/usb-pid for the full registry.
$PicoPids = @(
    @{ id = "2e8a:0003"; desc = "RP2040 BOOTSEL (mass storage)" },
    @{ id = "2e8a:0004"; desc = "PicoProbe (old firmware)" },
    @{ id = "2e8a:0005"; desc = "MicroPython" },
    @{ id = "2e8a:0009"; desc = "Pico SDK CDC UART" },
    @{ id = "2e8a:000a"; desc = "Pico SDK CDC UART (default app PID)" },
    @{ id = "2e8a:000b"; desc = "CircuitPython" },
    @{ id = "2e8a:000c"; desc = "Debug Probe (CMSIS-DAP)" },
    @{ id = "2e8a:000f"; desc = "RP2350 BOOTSEL (mass storage)" }
)

# Project-specific PIDs -- read from usb_ids.txt (single source of truth).
# The WSL launcher copies usb_ids.txt to the same TEMP directory as this script.
$UsbIdsPath = "$PSScriptRoot\usb_ids.txt"
$ApProxyVid = "1209"   # fallback if file not found
$ApProxyPid = "0001"
if (Test-Path $UsbIdsPath) {
    foreach ($line in (Get-Content $UsbIdsPath)) {
        if ($line -match '^AP_PROXY_VID=([0-9a-fA-F]+)') { $ApProxyVid = $Matches[1] }
        if ($line -match '^AP_PROXY_PID=([0-9a-fA-F]+)') { $ApProxyPid = $Matches[1] }
    }
} else {
    Write-Host "Warning: usb_ids.txt not found at $UsbIdsPath -- using defaults."
}
$ApProxyId = "$ApProxyVid`:$ApProxyPid"

$ProjectPids = @(
    @{ id = $ApProxyId; desc = "ap_proxy (pid.codes reserved test PID - internal use only)" }
)

$AllPids = $PicoPids + $ProjectPids

Write-Host "umod4 USB WSL setup"
Write-Host "-------------------"
Write-Host ""

$bound   = 0
$skipped = 0

foreach ($entry in $AllPids) {
    $out = usbipd.exe bind --hardware-id $entry.id 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Bound   $($entry.id)  $($entry.desc)"
        $bound++
    } else {
        Write-Host "  Skipped $($entry.id)  $($entry.desc) (not connected)"
        $skipped++
    }
}

Write-Host ""
Write-Host "Bound $bound device(s).  $skipped not connected (plug in and re-run to bind them)."
Write-Host ""
Write-Host "Current device states:"
usbipd.exe list

Write-Host ""
Write-Host "Done.  Bound devices will be shareable with WSL2 on future plug-ins."
Write-Host "VS Code and the test runner handle WSL attachment from this point on."

Stop-Transcript | Out-Null
