# Special Tools

This document tracks the special tools that are used when working with the umod4.

## Force WP into Bootsel Mode

If the umod4 has a debugger connected to the WP, the tool wp_enter_bootsel can be used to force the WP into BOOTSEL (mass storage) mode.
This is mainly for use with the automated testing scripts.

It uses OpenOCD to install a small RAM-based program that reboots the WP into BOOTSEL mode.

Requirements:

* Debugger wired to WP
* WSL only: debugger assigned to WSL

Usage:

    tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot

## Partition and Flash the WP

The tool 'tools/flash_WP' is used to completely reflash a WP.
The -e option can be used to totally erase the WP flash before starting.

This tool needs the WP to be in BOOTSEL mode.
It does not need a debugger to be attached.

The tool will optionally erase the entire WP flash, then install a partition table, and install WP firmware into the first image partition slot.
If the entire flash was erased, then the WP configuration data including the home network wifi credentials will be wiped.
This means that the WP will boot into AP mode, waiting for new home wifi network credentials to be installed.

If the erase option is not present, the WP will reboot using the existing credentials stored in the configuration partition.
If the wifi credentials are good, the WP will boot into STA (Station Mode), and connect to the home network.
If not, it will boot into AP mode, waiting for new credentials.

Requirements:

* The WP must be in BOOTSEL mode. A debugger is NOT required.

The flash_WP script will take care of reassigning the WP from Windows to WSL on WSL-based systems.

## ap_proxy

The ap_proxy program is used to give the host PC a way to connect to a umod4 that is operating in AP mode.
It allows the host PC to install the user's home wifi network credentials without requiring any manual steps from a human user.

The ap_proxy enables the automated test harness to completely erase & reflash a umod4 WP into AP mode, then validate the process of installing the WiFi credentials to get the WP connected to the home network.

### Flash ap_proxy to PicoW

To prepare a PicoW for installing ap_proxy, connect it to a PC in BOOTSEL mode (press and hold BOOTSEL while powering the PicoW).

With the PicoW in BOOTSEL mode, run the script "tools/flash_ap_proxy" to install the software in the picoW.

## Install WP WiFi Credentials

If you have a PicoW programmed with ap_proxy plugged into your PC, you can use the script "tools/set_credentials" to install WiFi credentials into a umod4 in AP mode without having to connect to the AP using your phone.

It exists as a timesaver if I need to put a umod4 into AP mode during bench tests, then assign its credentials again.

Usage:

python3 ~/projects/umod4/tools/set_credentials --name test --ssid BaHouse --password <passwd>

The ssid and password do not have to be specified on the command line if they are defined in your bash environment as:

```bash
UMOD4_WIFI_SSID=<your-network-ssid>
UMOD4_WIFI_PASSWORD=<your-network-password>
```
