# Umod4 Project

**You do not *need* an AI agent like Claude Code to work with this project, but it will make working with the project significantly easier if you are not a software developer.**

> **Using an AI coding assistant (for example Claude Code in VS Code)?**
> Paste the link to this page into it and tell it to read AGENTS.md first.
> If you are an AI coding agent: before doing anything else, read AGENTS.md in the repository root (https://github.com/mookiedog/umod4/blob/main/AGENTS.md).
> It explains who you are probably working with, what to ask them, and how to help them.

This page explains how to set up a development machine and build the umod4 firmware from source.
The firmware can only be built on Linux.
The reference setup is Windows 11 with WSL2 Ubuntu 24.04, where all development happens in VS Code: editing, building, flashing and debugging.
It can also be installed on a pure Linux machine: skip steps 2 to 5 of the [Prerequisites](#prerequisites) (they set up WSL), and skip the WSL networking, firewall and USB device sharing sections.

**Who this page is for:** experienced developers, plus AI agents setting up a machine for someone else.
It assumes you are comfortable with a terminal and git.
An agent can run the steps and explain anything unfamiliar; AGENTS.md section 3c lists the steps that only a person can do.
If you are a rider who wants to log and view rides, you do not need any of this: start with [GETTING_STARTED.md](GETTING_STARTED.md).

## Who needs what

| You want to | You need | You do **not** need | Where to look |
|-------------|----------|---------------------|---------------|
| Try the log visualizer on a sample ride | [the latest release zip](https://github.com/mookiedog/umod4/releases/latest) | WSL, a build environment, a umod4 board | Read the release zip's `README.txt` |
| Run the umod4 server and the log tools | [the latest release zip](https://github.com/mookiedog/umod4/releases/latest), on Windows 11 or Linux, and [a umod4 board](README.md#getting-a-board) | WSL, the firmware toolchains, a clone of the repository | [GETTING_STARTED.md](GETTING_STARTED.md), "Set up your PC" |
| Install the released firmware on a board | [the latest release zip](https://github.com/mookiedog/umod4/releases/latest), and [a umod4 board](README.md#getting-a-board) | WSL, the toolchains, the source code | [GETTING_STARTED.md](GETTING_STARTED.md), "Install the WP software" |
| Build the firmware from source (ECU, EP, WP) | everything on this page | a release zip | Read the remainder of this page |
| Debug the EP or WP on real hardware | the above, plus OpenOCD and a CMSIS-DAP probe | a release zip | "Install OpenOCD" and "WSL2: USB Device Sharing", below, then [DEBUGGING.md](DEBUGGING.md) and [BENCH_TESTING.md](BENCH_TESTING.md) |

## What the build produces

The project is designed as a CMake "superbuild", which builds for several subprojects, using different processors, in several languages:

1) 68HC11 assembly code for the ECU firmware
2) C, C++ and assembly language for the EP (RP2040, Cortex-M0+) and the WP (RP2350, Cortex-M33)
3) C, C++ and Python tools for the development host (an x86 PC)

Once everything is set up, rebuilding the entire system is as easy as pushing the `F7` key in VS Code.

## Versions known to work

| Component | Version | Where the version is decided |
|-----------|---------|------------------------------|
| Host OS, reference setup | Windows 11 with WSL2 Ubuntu 24.04 | tested by the author |
| Host OS, native Linux | Linux Mint 22.2 (based on Ubuntu 24.04) | tested by the author |
| Raspberry Pi Pico SDK | 2.3.1 | `PICO_SDK_VERSION` in the top-level `CMakeLists.txt` |
| pico-examples | the tag `sdk-` plus the SDK version | derived from the SDK version by CMake |
| FreeRTOS-Kernel | V11.3.0 | this document: pinned on purpose, see "FreeRTOS-Kernel" below |
| Arm GNU Toolchain (`arm-none-eabi`) | 15.2.rel1 (every release from 12 through 15 has built this project) | this document: the build uses the newest version installed under `/opt/arm/arm-none-eabi`, unless `ARM_NONE_EABI_VERSION` is set in `cmake/toolchains/arm-none-eabi.cmake` |
| GNU binutils for the 68HC11 | 2.42 | this document |
| OpenOCD | `raspberrypi/openocd` tag `sdk-2.2.0` (commit 04ae2c3) | this document |
| Python | 3.12 | `requires-python` in `pyproject.toml`; uv fetches it if needed |
| picotool | built by the superbuild | nothing to install |

A newer release of the compiler or of binutils should work, but has not been tested here.

## Troubleshooting

If you hit a wall during this process, open an issue at [github.com/mookiedog/umod4/issues](https://github.com/mookiedog/umod4/issues).
Use the issue reporting system to describe what step you're on, what problem you have encountered or what error you are seeing, and I'll try to help.
Reporting the issues will help make this setup process as simple and accurate as possible.
The issue tracker is public, so leave out anything private, such as passwords, your WiFi name, or where you live.

## Prep The Development System

The list of the high-level steps to get a build system working on Windows is shown below.
It's a long list, but it's not overly difficult, and it only needs to happen once.

The high-level steps:

* [Prerequisites](#prerequisites): you must do these yourself
  * Install VS Code (step 1)
  * Install WSL2 and connect VS Code to it (steps 2 to 5)
  * Install your AI assistant (step 6)
  * Download the project and open it (steps 7 and 8)
  * Start your AI assistant (step 9)
* [Build Environment Setup](#build-environment-setup): an AI assistant can do these for you
  * [Set up WSL Networking](#set-up-wsl-networking)
  * [Set up Windows Firewall](#add-windows-networking-firewall-rules)
  * [Install the remaining VS Code extensions](#vs-code-extensions)
  * [Install Linux software](#linuxwsl2-software-installation)
    * [Host Tools](#install-linux-host-tools)
    * [Python and uv](#python-and-uv)
    * [Build a 68HC11 toolchain](#build-68hc11-toolchain)
    * [Install ARM Tools](#install-arm-cross-compiler-toolchain)
    * [Build OpenOCD](#install-openocd)
  * [Share USB devices with WSL](#wsl2-usb-device-sharing)
  * [Install the other software the project needs](#project-development-setup)
    * [Raspberry Pi Pico SDK](#rpi-sdk)
    * [pico-examples](#pico-examples)
    * [FreeRTOS-Kernel](#freertos-kernel)

Once all the software is successfully installed, you will use VS Code to:

* [Configure the project build](#configuration)
* [Build the project software](#building)
* Put the firmware onto a [umod4 circuit board](#running-umod4)

The following sections detail how to do the items listed, above.

## Prerequisites

Even if you intend to work with this project via an AI agent, you must do everything in this section yourself, because it is what gets your machine set up to the point that an AI assistant can be used.

The umod4 build tools run on Linux.
Windows can run Linux inside itself using WSL2 (Windows Subsystem for Linux).
You do not need a second computer and you do not need to dual-boot your Windows machine.

You need:

* Windows 11 version 22H2 or later.
  Windows 10 is not supported: the networking setup that lets the umod4 find the server needs a Windows 11 feature (see [WSL networking](#set-up-wsl-networking)).

  **Check:** press the Windows key and R together, type `winver`, and press Enter.
  The window that opens should say Windows 11, version 22H2 or later (any later version, such as 25H2, is fine).
  If it says Windows 10, or an older Windows 11 version, update Windows before going any further.
* Administrator rights on the PC

Strongly recommended:

* A Claude account with access to Claude Code (as of 2026/11, that means Claude Pro at a minimum)

### Initial Installation Steps

**1. Install VS Code on Windows.**
VS Code is the IDE the whole project is built and debugged from.
It runs as a Windows app, but seamlessly interacts with the Linux environment you are about to install.
Download the VS Code installer from [code.visualstudio.com](https://code.visualstudio.com/download) and run it with the default options.
If you already have VS Code, skip this step.
(On a Linux PC without Windows, install VS Code with the .deb or .rpm package from the same site.)

**2. Install WSL2.**
WSL2 runs Linux inside a virtual machine, so your PC's hardware virtualization feature must be turned on.
Most PCs ship with it on, but some do not, and this is the most common reason that a WSL install fails.
Check first: press Ctrl+Shift+Esc to open Task Manager, click Performance, then click CPU.
The line labelled "Virtualization" should say "Enabled".

If it says "Disabled", or if the install below fails with an error that mentions virtualization, you need to turn it on in your PC's BIOS/UEFI setup:

* Restart the PC and press the setup key while it starts. It is usually Del, F2, F10 or Esc, depending on the PC maker.
* Find the setting named "Intel Virtualization Technology" or "Intel VT-x" (Intel CPUs), or "SVM Mode" or "AMD-V" (AMD CPUs). It is often under an Advanced or CPU menu.
* Enable it, then save and exit.

The menus are different on every PC. If you cannot find it, search the web for your PC model plus "enable virtualization".

Now install WSL2.
Press the Windows key, type `PowerShell`, right-click "Windows PowerShell", and choose "Run as administrator".
Type the following and press Enter:

```powershell
wsl --install
```

This installs WSL2 itself and the Ubuntu distribution of Linux.
Restart the PC when it asks you to.
If you see WSL's help text instead of an install, WSL is already present: run `wsl --list --online` to see the available distributions, then `wsl --install -d Ubuntu`.
The official Microsoft instructions are [here](https://learn.microsoft.com/en-us/windows/wsl/install).

**3. Finish the Ubuntu setup.**
After the restart, an Ubuntu window should open by itself.
If it does not, open the Start menu and launch "Ubuntu".
It will ask you to choose a Linux username and password.
They do not have to match your Windows username and password.
When entering your password, nothing appears on the screen while you type it.
That is normal.
Remember the new Linux password: you will need it whenever Linux software has to be installed.

**4. Install the WSL extension in VS Code.**
Start VS Code by typing 'code' in the Win 11 search box, then clicking on the Visual Studio Code app icon when it is displayed.
After VS Code starts up, click the Extensions icon in the left-hand bar (it looks like four squares, with one of them detached), search for "WSL" (publisher: Microsoft), and install it.
If you cannot find the icon, press F1, type `Extensions: Install Extensions`, and press Enter.

**5. Open VS Code inside Linux.**
Press F1, type `WSL: Connect to WSL`, and press Enter.
It may take a bit of time to start WSL the first time, but after a delay, the bottom-left corner of the window should say "WSL: Ubuntu".
**Everything from here on happens in the VS Code window which is connected to WSL.**

**6. Install the Claude extension inside Linux.**
In the WSL Code window, open Extensions again, find "Claude Code for VS Code", and click "Install in WSL: Ubuntu".
Extensions installed on the Windows side are not automatically available inside Linux, so this step is required.
Sign in to Claude when asked.

**7. Download the project.**
In the WSL window, open a terminal via Ctrl+` or Terminal > New Terminal.
Type these commands into the terminal window:

```bash
git config --global core.autocrlf false
mkdir -p ~/projects
git clone https://github.com/mookiedog/umod4.git ~/projects/umod4
```

`core.autocrlf false` stops git from converting the repository's LF line endings to Windows CRLF.

Use the project location **exactly as specified**.
The installation and software build processes assume that the project lives in `~/projects/umod4`.
**It is not recommended to change it.**

**8. Open the project.**
Choose File > Open Folder and select `/home/<your-username>/projects/umod4`.
If VS Code asks "Do you trust the authors of the files in this folder?", click "Yes".

**9. Start your assistant.**
If you have access to Claude Code through a subscription, open the Claude panel and send this message:

> Read AGENTS.md and BUILDING.md, then help me get this project set up.

The assistant will ask what you want to do with the project and take it from there, helping you with everything remaining in this document.

If you do not have access to a Claude Code subscription, keep working through the remainder of this document as per the instructions.

### Problems

Virtualization errors: see the virtualization check at the start of step 2.
Anything else: paste the exact error text into your AI assistant, or see [Troubleshooting](#troubleshooting).

## Build Environment Setup

An AI assistant can do nearly all of the remaining installation and setup work, stopping only for what needs you: entering a Linux password, obtaining Windows administrator rights, or plugging in hardware.
Alternatively, if you have some software experience, you can do it all yourself by just following the instructions.

### Set Up WSL Networking

The umod4 finds the server by broadcasting on your local network.
In WSL2's default networking mode those broadcasts never reach Linux, so the server would never see the umod4 device.
'Mirrored' mode makes WSL share your PC's IP address and network interface instead.
This feature requires Windows 11 version 22H2 or later ([Microsoft documentation](https://learn.microsoft.com/en-us/windows/wsl/wsl-config)).

Add these lines to `C:\Users\<your-user-name>\.wslconfig`.
Create the file if it does not exist, or add only the second line under an existing `[wsl2]` section:

```txt
[wsl2]
networkingMode=mirrored
```

Then restart WSL from a PowerShell window:

```powershell
wsl --shutdown
```

This closes every WSL window, including the VS Code window that is connected to WSL.
Reopen it afterwards (F1, `WSL: Connect to WSL`) or click the connect icon in the lower left corner of the VS Code window.

**Verify:** in a WSL terminal, this should print `mirrored`:

```bash
wslinfo --networking-mode
```

### Add Windows Networking Firewall Rules

The device checks in with the server over UDP port 54549, so Windows must allow that traffic in.
This is the only rule needed: the server connects out to the device, nothing connects in over TCP, and Windows allows outbound connections by default.

In a PowerShell window opened as Administrator (as in step 2 above):

```powershell
New-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" -Direction Inbound -Protocol UDP -LocalPort 54549 -Action Allow -Profile Any
```

The same rule can be made in the Windows Defender Firewall GUI: Advanced settings, Inbound Rules, New Rule, Port, UDP 54549, Allow the connection, all three profiles.

**Verify:** this should show `Protocol : UDP` and `LocalPort : 54549`:

```powershell
Get-NetFirewallRule -DisplayName "umod4 Server UDP Check-in" | Get-NetFirewallPortFilter
```

## VS Code Extensions

Steps 4 and 6 above already installed the WSL and Claude extensions.
These are the rest of the extensions the project needs:

* C/C++ and C/C++ Extension Pack (Microsoft): `ms-vscode.cpptools`, `ms-vscode.cpptools-extension-pack` (the pack also brings in CMake Tools)
* Cortex-Debug: `marus25.cortex-debug`
* MemoryView: `mcu-debug.memory-view`
* RTOS Views: `mcu-debug.rtos-views`
* Markdown Preview Github Styling: `bierner.markdown-preview-github-styles`
* markdownlint: `davidanson.vscode-markdownlint`
* Python (Microsoft): `ms-python.python`

The required extensions can be installed all at once using the VS Code terminal window from step 5:

```bash
code --install-extension ms-vscode.cpptools \
     --install-extension ms-vscode.cpptools-extension-pack \
     --install-extension marus25.cortex-debug \
     --install-extension mcu-debug.memory-view \
     --install-extension mcu-debug.rtos-views \
     --install-extension bierner.markdown-preview-github-styles \
     --install-extension davidanson.vscode-markdownlint \
     --install-extension ms-python.python
```

The install command is harmless if one of the extensions in the install list is already installed.

The manual method would be to search for each name in the Extensions panel and click "Install in WSL: Ubuntu".

Once installed, force VS Code to activate the new extensions.
Press F1, type `Developer: Reload Window`, and press Enter to activate them.

**Verify:** this should list all eight extension IDs above (along with others):

```bash
code --list-extensions
```

## Linux/WSL2 Software Installation

Run the Linux commands in this document in a terminal in the VS Code WSL window (Ctrl+`, or Terminal > New Terminal).

Bring Ubuntu up to date first.
`sudo` asks for the Linux password you chose in step 3, and the first run may install many updates:

```bash
sudo apt update
sudo apt upgrade
```

### Create a Local Bin Directory

The build installs a few helper executables into `~/.local/bin` instead of a system-wide location, so that directory must be on your PATH:

```bash
mkdir -p ~/.local/bin
. ~/.profile
echo $PATH | tr ':' '\n' | grep '[.]local'
```

**Verify:** the output should include `/home/<your-user-name>/.local/bin`.

If it does not, your `~/.profile` is not adding that directory to the PATH.
Append the lines that do, then repeat the check:

```bash
cat >> ~/.profile << 'EOF'
# set PATH to include a user's private bin directory if it exists
if [ -d "$HOME/.local/bin" ] ; then
    PATH="$HOME/.local/bin:$PATH"
fi
EOF
. ~/.profile
```

### Install Linux Host Tools

These tools run on the host PC.
Some may already be installed; asking again is harmless:

```bash
sudo apt install gcc g++ git curl unzip cmake jq ninja-build libncurses5-dev libncursesw5-dev
```

`gcc` and `g++` here build code for your Linux **host**, not for the Pico's ARM chips.

**Verify:** this should print a path for each of the eight tools:

```bash
command -v gcc g++ git curl unzip cmake jq ninja
```

#### Python and uv

The Python tools (the visualizer, the server and the test harness) run in a virtual environment managed by [uv](https://docs.astral.sh/uv/).
The packages are listed in `pyproject.toml`, their exact versions are pinned in `uv.lock`, and the environment lives in `.venv` at the top of the repository.
The build creates and updates it (it runs `uv sync`), so installing uv is the only manual step.
uv also downloads Python 3.12 the first time it needs it, so you do not need to install Python or the `python3-venv` package.

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

The installer puts `uv` in `~/.local/bin`, which is on your PATH if you did "Create a Local Bin Directory", above.

**Verify:** in a new terminal, this should print a version number:

```bash
uv --version
```

Things worth knowing:

* Run Python tools with `uv run <script>` from anywhere inside the repository. There is no virtual environment to activate.
* The Python version is pinned to 3.12 on purpose. The comment above `requires-python` in `pyproject.toml` explains why, and lists the steps to change it.
* The downloaded Python is kept in `~/.local/share/uv/python`, shared by every uv project, not inside `.venv`.
* `.venv` is large (about 400 MB), mostly the Qt libraries (PySide6) used by the visualizer and the server GUI. That is expected.

### Build 68HC11 Toolchain

The Gen 1 ECU's processor is a Motorola 68HC11, and GCC has not had a C compiler for it since version 3.4.6, in the early 2000's.
That does not matter here: the UM4 firmware is pure assembly, so the build only needs the assembler, linker and objcopy from GNU binutils, and modern binutils can still target the 68HC11.
A 68HC11 build of binutils cannot be installed with apt, so you build it from source, which takes a couple of minutes.

The version known to work is 2.42, listed in "Versions known to work" above.
Newer versions should work, but have not been tested here.
Releases are listed on the [GNU Binutils](https://www.gnu.org/software/binutils/) page.
The source goes in `~/binutils/binutils-m68hc11-elf-<version>`, so that several versions can coexist.
The tools install into `~/.local/bin`, which must be on your PATH (see "Create a Local Bin Directory", above).

```bash
BINUTILS_VERSION=2.42
sudo apt install texinfo
mkdir -p ~/binutils/binutils-m68hc11-elf-$BINUTILS_VERSION
cd ~/binutils
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz
tar zxf binutils-$BINUTILS_VERSION.tar.gz --strip-components=1 -C binutils-m68hc11-elf-$BINUTILS_VERSION
cd binutils-m68hc11-elf-$BINUTILS_VERSION
./configure --prefix=$HOME/.local --target=m68hc11-elf
make && make install
```

**Verify:** the first command should print `GNU assembler (GNU Binutils) 2.42` (or the version you built), and the second should print a path for each of the two tools:

```bash
m68hc11-elf-as --version | head -1
command -v m68hc11-elf-ld m68hc11-elf-objcopy
```

If a command is not found, `~/.local/bin` is not on your PATH.

### Install ARM Cross-Compiler Toolchain

A cross-compiler runs on your x86 PC but generates code for the ARM processors inside the umod4: the RP2040 in the EP and the RP2350 in the WP.

The build expects the toolchains under `/opt/arm/arm-none-eabi/<version>`, one subdirectory per version.
It uses the newest version it finds there, unless `ARM_NONE_EABI_VERSION` is set in `cmake/toolchains/arm-none-eabi.cmake`.
The current version is 15.2.rel1, listed in "Versions known to work" above, and every release from 12 through 15 has built this project.
A newer release should work, but has not been tested here.

The download is about 150 MB:

```bash
ARM_VERSION=15.2.rel1
curl -fL -o /tmp/arm-toolchain.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VERSION/binrel/arm-gnu-toolchain-$ARM_VERSION-x86_64-arm-none-eabi.tar.xz \
  && sudo mkdir -p /opt/arm/arm-none-eabi/$ARM_VERSION \
  && sudo tar xf /tmp/arm-toolchain.tar.xz --strip-components 1 -C /opt/arm/arm-none-eabi/$ARM_VERSION \
  && rm /tmp/arm-toolchain.tar.xz
```

The `&&` chain matters: the build picks the newest directory under `/opt/arm/arm-none-eabi`, so a failed download must not leave an empty one behind.
The `--strip-components 1` removes the long directory prefix that Arm buries inside the archive.

If that `developer.arm.com` URL stops working, get the link from the [Arm GNU Toolchain download page](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads): under *x86_64 Linux hosted cross toolchains* → *AArch32 bare-metal target (arm-none-eabi)*, right-click the `.tar.xz` file, choose "Copy link address", and use that link in place of the URL above.
The version number is in the filename.

**Verify:** both commands should print a line containing the version you installed, for example `Arm GNU Toolchain 15.2.Rel1`.
The other numbers on the line (the compiler or debugger version) differ between the two tools:

```bash
/opt/arm/arm-none-eabi/$ARM_VERSION/bin/arm-none-eabi-gcc --version | head -1
/opt/arm/arm-none-eabi/$ARM_VERSION/bin/arm-none-eabi-gdb --version | head -1
```

Do **not** add the `bin` directory to your PATH.
The umod4 build system finds it automatically from the `/opt/arm/arm-none-eabi` layout.

### Install OpenOCD

*At some point, the RP2350 support will exist in the Ubuntu distribution and installing OpenOCD will be as simple as "sudo apt install openocd".
But for now, it needs to be built from source, as described below.*

OpenOCD is the "Open On-Chip Debugger" software tool.
GDB uses OpenOCD to talk to the silicon debug unit inside the chip being debugged.
We need a special version of OpenOCD to properly deal with the dual core RP2040 and RP2350 processors used by the umod4 project.
The version known to work is the Raspberry Pi fork's `sdk-2.2.0` tag, listed in "Versions known to work" above.
A newer tag, `sdk-2.3.0`, exists but has not been tested here.

Install OpenOCD from the raspberrypi repository to get RP2xxx support:

```bash
git clone --branch sdk-2.2.0 --recurse-submodules https://github.com/raspberrypi/openocd.git ~/projects/openocd
```

Building OpenOCD requires a few more packages:

```bash
sudo apt install libusb-1.0-0 libusb-1.0-0-dev libhidapi-dev libtool autoconf automake texinfo pkg-config make
```

Then build and install it:

```bash
cd ~/projects/openocd
./bootstrap
./configure --enable-ftdi --enable-sysfsgpio --enable-bcm2835gpio --enable-cmsis-dap --enable-internal-jimtcl
make && sudo make install
```

OpenOCD needs to access the USB Pico Debug probe.
We create a rules file that allows you to access the Pico Probe USB device without `sudo`:

```bash
sudo tee /etc/udev/rules.d/46-probe.rules > /dev/null << 'EOF'
# Pi Pico CMSIS-DAP USB debug probe
ATTRS{idProduct}=="000c", ATTRS{idVendor}=="2e8a", MODE="666", GROUP="plugdev"
EOF
```

Activate the new rules file:

```bash
sudo udevadm control --reload
sudo udevadm trigger
```

**Verify:** the first command should print `Open On-Chip Debugger 0.12.0+dev` followed by a build date.
The second should print the paths of the RP2040 and RP2350 target files; it fails if OpenOCD lacks support for either chip (the stock Ubuntu 24.04 package has no `rp2350.cfg`).
The third should print `plugdev`:

```bash
openocd --version | head -1
openocd -c "find target/rp2040.cfg" -c "find target/rp2350.cfg" -c shutdown
id -nG | grep -w plugdev
```

If the third command prints nothing, add yourself to the group with `sudo usermod -aG plugdev $USER`.
A new group only reaches terminals that are started afterwards.
A new tab in an ordinary terminal window is enough.
A terminal that was already open inside VS Code is not: restart VS Code, which takes you straight back to where you were.
Running `wsl --shutdown` in PowerShell and reopening WSL also works, and covers every case.
On native Linux, log out and back in instead.

The automated test harness (see [tests/README.md](tests/README.md)) also talks to a small Raspberry Pi Pico W, the ap_proxy, over a USB serial port.
Opening that port needs membership in a second group, `dialout`.
You only need this if you will run the test harness.

**Verify:** this should print `dialout`:

```bash
id -nG | grep -w dialout
```

If it prints nothing, add yourself to the group with `sudo usermod -aG dialout $USER`, then start a fresh terminal as described for `plugdev`, above.

## WSL2: USB Device Sharing

*If you are on native Linux (not Windows/WSL2), skip this section.*

Windows owns every USB device plugged into it, so WSL cannot see them until `usbipd` shares them.
There are two operations.
**Bind** is a one-time grant, made by an Administrator, that lets a device type be shared with WSL; the device then shows as `Shared`, and it stays that way across unplugging and reboots.
**Attach** connects a shared, plugged-in device to WSL each session, and `flash_WP` and the test runner do that for you.
So you only need to bind, once per device type.

### Install usbipd

Install usbipd-win once, from a PowerShell window opened as Administrator (as in step 2 above):

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

**Verify:** in a WSL terminal, this should print a version number:

```bash
usbipd.exe --version
```

### Bind the Devices

With your umod4 hardware (or debug probe) plugged in, run this in a WSL terminal:

```bash
~/projects/umod4/tools/setup_usb_wsl
```

A Windows permission prompt (UAC, User Account Control) will appear on the Windows desktop.
Click **Yes** to allow the script to run as Administrator.
The script binds all the Raspberry Pi Pico-family devices (including the CMSIS-DAP debug probe and the RP2350 BOOTSEL mass-storage device), plus the ap_proxy device used by the automated test harness.
Devices must be plugged in when it runs, so run it again whenever you add a device that was not plugged in the first time.

**Verify:** with the debug probe plugged in, this should list it (`2e8a:000c`) with the state `Shared`, or `Attached` if something has already attached it to WSL:

```bash
usbipd.exe list
```

## Project Development Setup

The build finds the SDK, pico-examples and FreeRTOS-Kernel by location: it computes their paths from the directory that contains the umod4 repository, so all of them must sit side by side in `~/projects`.
No environment variables are needed.

```text
/home/<your-user-name>/
└── projects/
    ├── FreeRTOS-Kernel   (one checkout, pinned to V11.3.0)
    ├── openocd           (the Raspberry Pi fork, built earlier)
    ├── pico-examples
    │   └── 2.3.1         (git tag sdk-2.3.1)
    ├── pico-sdk
    │   └── 2.3.1         (git tag 2.3.1: the version this project builds with)
    └── umod4             (this repository)
```

The SDK is large (about 800 MB), so every project on the machine shares one copy.
Each SDK version lives in its own subdirectory, so older projects can keep using older versions.

### RPi SDK

The version to use is `PICO_SDK_VERSION` in the top-level `CMakeLists.txt` (currently 2.3.1).
Do not pick a newer version just because one exists: the project is only known to build with the version it asks for.
The SDK's submodules supply the WiFi and Bluetooth code.

```bash
SDK_VERSION=2.3.1
mkdir -p ~/projects/pico-sdk
git clone --branch "$SDK_VERSION" --recurse-submodules https://github.com/raspberrypi/pico-sdk ~/projects/pico-sdk/"$SDK_VERSION"
```

**Verify:** the first command should print `2.3.1`, and the second should print `0` (the number of submodules that were not fetched):

```bash
git -C ~/projects/pico-sdk/"$SDK_VERSION" describe --tags
git -C ~/projects/pico-sdk/"$SDK_VERSION" submodule status | grep -c '^-'
```

#### pico-examples

The `pico-examples` repository is **required**, not optional sample code.
The WP firmware builds `ws2812.pio` from it, and the `WP_erase` tool builds `nuke.c` from it.
Like the SDK, it needs one checkout per SDK version, and the tag is `sdk-` plus the same number as `PICO_SDK_VERSION`.
The match matters because `nuke.c` differs between SDK releases.

```bash
SDK_VERSION=2.3.1
mkdir -p ~/projects/pico-examples
git clone --branch "sdk-$SDK_VERSION" --depth 1 https://github.com/raspberrypi/pico-examples.git ~/projects/pico-examples/"$SDK_VERSION"
```

**Verify:** this should print `sdk-2.3.1`:

```bash
git -C ~/projects/pico-examples/"$SDK_VERSION" describe --tags
```

If the directory is missing, CMake stops and prints the clone command to use.
If the checkout is on a different tag than the SDK, CMake warns and prints the command that fixes it.
To keep the checkout somewhere else, pass `-DPICO_EXAMPLES_PATH=<dir>` to cmake.

#### FreeRTOS-Kernel

FreeRTOS-Kernel is **not** part of any pico-sdk release, so it is its own checkout, next to `pico-sdk` and not inside a specific SDK version.
Its releases are independent of the SDK's, so one checkout serves every SDK version.

```bash
git clone --branch V11.3.0 --recurse-submodules https://github.com/FreeRTOS/FreeRTOS-Kernel ~/projects/FreeRTOS-Kernel
```

V11.3.0 is pinned on purpose: it is the version this project has been built and tested against, and it includes the `RP2350_ARM_NTZ` community port (from the `Community-Supported-Ports` submodule) that the WP needs.
**Do not move to a newer release without re-testing a full WP build on real hardware.**

**Verify:** the first command should print `V11.3.0`, and the second should list the port directory (it only exists if the submodules were fetched):

```bash
git -C ~/projects/FreeRTOS-Kernel describe --tags
ls -d ~/projects/FreeRTOS-Kernel/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ
```

### Changing the SDK Version

Old SDK versions can stay where they are: another project may still need one.
After moving from 2.3.0 to 2.3.1, for example, the layout looks like this.
The build uses whichever version `PICO_SDK_VERSION` names:

```text
/home/<your-user-name>/
└── projects/
    ├── FreeRTOS-Kernel
    ├── openocd
    ├── pico-examples
    │   ├── 2.3.0         (git tag sdk-2.3.0)
    │   └── 2.3.1         (git tag sdk-2.3.1)
    ├── pico-sdk
    │   ├── 2.3.0         (git tag 2.3.0)
    │   └── 2.3.1         (git tag 2.3.1)
    └── umod4
```

To move this project to a new version, clone the new SDK beside the old one as above and clone the matching `pico-examples` tag.
Then change `PICO_SDK_VERSION` in the top-level `CMakeLists.txt`, and delete the `build` directory: the comment next to `PICO_SDK_VERSION` warns that a version change requires it.
Everything else, including VS Code's debug SVD file lookup, follows from that one setting.
An SDK change can alter the machine code of the EP and the WP, so treat it like any other EP change: run the test runner and the bench checks ([BENCH_TESTING.md](BENCH_TESTING.md)) before the new build goes near a bike.

The SDK also decides which picotool it needs, and it refuses to continue if the picotool installed in `~/.local` is a different version.
The superbuild builds the matching picotool, but a build can stop on an older one before that happens, with the message `Incompatible picotool installation found`.
If you see it, run `ninja picotool` in the `build` directory, which builds and installs the right version over the old one, and then build again.

## Getting the Umod4 Source Code

If you already cloned the project in step 7 of the [Prerequisites](#prerequisites), skip this block.
On native Linux, where those steps do not apply, clone it now:

```bash
mkdir -p ~/projects
git clone https://github.com/mookiedog/umod4.git ~/projects/umod4
```

**Verify:** the project is in place, next to the software installed above, and clean:

```bash
git -C ~/projects/umod4 status
ls ~/projects
```

The first command should end with `nothing to commit, working tree clean`.
If it lists thousands of changed files, git converted the line endings: set `core.autocrlf` to `false` as in step 7 and clone again.
The second should list `FreeRTOS-Kernel`, `openocd`, `pico-examples`, `pico-sdk` and `umod4` side by side.

If you ever make a mess of the repository, you can erase it and start over with a fresh clone.
This destroys any changes that you have not pushed somewhere else:

```bash
rm -rf ~/projects/umod4
```

## Building Umod4

### Configuration

Configuring runs CMake, which analyzes the whole project and generates the files that tell the build system how to create the outputs.
Configuring needs internet access: it downloads several small libraries and picotool from GitHub.
In VS Code, press F1, type `delete`, and choose **CMake: Delete Cache and Reconfigure**.
If a window asks you to select a kit (a toolchain), choose "unspecified": the project sets up its own toolchains.
From a terminal, the equivalent is:

```bash
cd ~/projects/umod4
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build -G Ninja
```

**Verify:** the messages should end with a line like this, which means no errors:

```text
-- Build files have been written to: /home/<your-user-name>/projects/umod4/build
```

### Building

In VS Code, press F7 with the cursor inside the window.
Alternatively, from a VS Code terminal window:

```bash
ninja -C ~/projects/umod4/build
```

The first build takes from about 30 seconds to a few minutes, depending on your PC's capabilities.
After that, builds only redo what changed, which typically takes only a few seconds.

The first build may stop with an error saying that the picotool udev rules are not installed.
These rules are required so that picotool can reach the Pico's USB devices without `sudo`.
The error message shows the exact commands to run: run them, then hit `F7` to build again.

**Verify:** in VS Code, the output should end with `[build] Build finished with exit code 0`.
From a terminal, `ninja` should finish without an error.
If a step fails, scroll back to the first error message in the output: that is the one to fix.

### The 'build' Directory

Everything that the build generates goes into `~/projects/umod4/build`, which keeps it completely separate from the sources.
The one exception is the Python virtual environment, which lives in `.venv` at the top of the repository so that it survives deleting `build`.
Both can be re-created automatically, so it is always safe to delete them, and a truly clean starting point means deleting both.

Sometimes the CMake build gets out of sync, usually after a `CMakeLists.txt` was changed.
First try **CMake: Delete Cache and Reconfigure** again.
If that does not help, delete the build directory and rebuild:

```bash
rm -rf ~/projects/umod4/build
```

Then reconfigure and build as above.
Note that VS Code's **CMake: Clean Rebuild** does not clean the EP, WP and the other subprojects, because each is a separate CMake project inside `build`: delete `build` (or just `build/<name>`) instead.

### Build Outputs

A successful build generates many files.
The most important ones are:

* **build/EP/EP.uf2:** the EP firmware image
* **build/WP/WP.uf2:** the WP firmware image
* **build/WP_erase/WP_erase.uf2:** a small program that runs from RAM and erases the whole flash of a WP.
* **build/WP/WP_partition.uf2:** the flash partition table for a blank WP.
* **build/ecu/UM4.bin:** An ECU EPROM image containing the data-logging ECU code with default 549USA maps. Every EP.uf2 contains a copy as its default built-in EPROM image. Upload UM4.bin to the EP's image library if you want to keep a specific version.
* **build/ecu/sparky.bin:** A bench-only ECU image built from the same source as UM4.bin with `BENCH_IGNITION_TEST` defined. It skips the real ECU mainloop and just fires both front and rear ignition coils as though the engine were idling at 1200 RPM. It is not included in EP.uf2; upload it to the EP's image library or flash it into a 29C256 if you want to use it.

**Verify:** this should list all six files:

```bash
cd ~/projects/umod4/build
ls -l EP/EP.uf2 WP/WP.uf2 WP_erase/WP_erase.uf2 WP/WP_partition.uf2 ecu/UM4.bin ecu/sparky.bin
```

`WP_erase.uf2`, `WP_partition.uf2` and `WP.uf2` are the three files that get dragged onto a blank WP in BOOTSEL mode for the first install: see [Install the WP software](./GETTING_STARTED.md#install-the-wp-software).

The server program will reflash the umod4 OTA (Over The Air) using the EP.uf2 and WP.uf2 files.

#### Getting the files from Linux to Windows

The build runs inside Linux, but the BOOTSEL drag-and-drop happens in a Windows Explorer window, so you need to reach the Linux files from Windows:

* In a WSL terminal, `cd` to the directory holding the files (for example `cd ~/projects/umod4/build/WP`) and type `explorer.exe .`: an Explorer window opens there.
* Or browse to `\\wsl.localhost\Ubuntu\home\<your-username>\projects\umod4\build` in Explorer (if you gave your Linux distribution a name other than `Ubuntu`, use that name).
* Going the other way, your Windows C: drive appears in Linux as `/mnt/c`.

## Running Umod4

The next step would be to flash the WP software onto a umod4 circuit board.
If you have a umod4 board, check out [GETTING_STARTED.md](./GETTING_STARTED.md) to see how to prep your ECU and get the software installed onto it.

To debug the EP or WP on real hardware using VS Code and a Pico Debug unit, you also need a bench setup: see [DEBUGGING.md](./DEBUGGING.md) and [BENCH_TESTING.md](./BENCH_TESTING.md).

If you do not have a board, there are still plenty of areas of the project to explore though.
Check out the project's [README](./README.md) for a high-level tour of the various pieces.

## Final Thoughts

Congratulations if you made it this far.
I know it was not a simple process to get to this point.

To give credit where it is due, I only got it working due to tons of people on the internet who ran into all the same problems as I did, but somehow were smart enough to figure out the issues, and more critically, to document their successes in places where DuckDuckGo could find them for me.
And credit to Claude Code, too: Claude knows CMake & Python, for sure.

I will probably always be working on some aspect of this project because that's what "fun" looks like to me.
Weird, I know, but fun comes in many forms.

![image](doc/images/README/proudly-made-1.jpg)
