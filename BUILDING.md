# Umod4 Project

This document explains how to set up a machine so that it can build the umod4 project.

To set expectations: if you are a Gen1 Aprilia enthusiast more than a software person, be warned that this project is not trivial.
If you have worked with Arduino, I can tell you that it's significantly more complicated than loading an Arduino sketch and going riding!
To get the project working requires building tools and executables for four separate processors:

1) 68HC11 assembly code for ECU firmware
1) C, C++ and assembly language for ARM Cortex M0+: the EP and WP processors (both running dual-core RP2040 processors)
1) various C, C++, and Python tools that will run on the development host (an x86 PC or ARM Raspberry Pi)

## Development System

Linux is required to build this project.
You do not need a pure linux machine to work with it though.
This project has been developed and tested on three different OS/machine combinations:

1) an x86-64 Windows machine running Windows Subsystem for Linux (WSL2)
1) an x86-64 machine running Linux Mint 24
1) a ARM-based Raspberry Pi 5 running Raspberry Pi OS (Linux)

The project uses Microsoft's Visual Studio Code as its IDE.
VS Code is available for x86 and ARM, and runs under Windows WSL2 as well as other linux distros.
It might be possible to use an IDE other than VS Code, but that would be kind of a big change, and you are on your own if you do.
Certain aspects of the build/run process are baked into VS Code setup files like c_cpp_properties.json and launch.json.
These would need to be identified and changed if you were to use a different IDE.

## Prep The Development System

The list of the high-level steps to get a build system working on Windows is shown below.
The instructions assume that you will be using a Window machine and running WSL2 linux under Windows.
If you choose to build the system on a pure Linux machine instead of Windows/WSL2, then you probably already know what you are doing and can figure out what needs to happen based on these WSL2 instructions.

* Install Windows software
  * VS Code
    * Install a bunch of VS Code extensions
  * WSL2 Ubuntu Linux
  * Windows Terminal app
* Install Linux software under WSL2
  * Python Virtual Environment support
  * Git
  * C/C++ host compilers
  * Build a 68HC11 toolchain from source (requited to create Aprilia ECU software)
  * C/C++ cross compilers (to create Arm Cortex code that runs on umod4 hardware)
  * GDB (GNU debugger), for debugging ARM Cortex code
  * CMake, which creates the build system for umod4
  * Ninja, used by CMake to drive the actual build process
* Install software required by umod4 source code
  * Raspberry Pi Pico-SDK
    * enable wifi and bluetooth extensions
  * FreeRTOS for Pi Pico
    * Needs a special version that includes SMP support for the RP2040 dual-core processors
* Install umod4 source code

Once all the software is successfully installed, you will be able to use VS Code to:

* Configure the umod4 build
* Build all the umod4 software
* Use a hardware debugger to flash the code onto a umod4 circuit board

It's a bit of a trek to get there, but here we go...

## Windows Software Installation

There are a few programs that need to be installed on the Windows machine.

*If you are developing strictly using a linux machine, all you need to install is VS Code for linux, along with all the extensions mentioned in the VS Code section, below.
After that, you can skip to the section "Linux/WSL2 Software Installation".*

### VS Code

VS Code is a software IDE (Integrated Development Environment) that runs in Windows.
Basically, it is an extremely powerful text editor, with all kinds of additions to help you develop writing code.
Even though VS Code runs under Windows, it is able to seamlessly connect to the Linux enviroment we will be creating to actually develop the umod4 project.

Installing VS Code is easy.
Official Microsoft instructions are located [here](https://code.visualstudio.com/docs/setup/windows).
If that link goes dead, just google 'installing VS Code', and find a link that tells you how to do it.

Once you have VS Code installed, you need to add a bunch of extensions, as described in the next section.

#### VS Code Extensions

One of VS Code's best features is that it is amazingly extensible.
People all over the world write useful "extensions" that add new features to the editor.
This project needs a bunch of extensions to be installed.
Start VS Code.
You don't need to open any directories.
Click the 'extensions' icon on the left side ribbon.
The 'extensions' icon looks like 3 boxes with a 4th box floating above the 3 boxes.
Once you find that icon, click it.
It will open a search box that says 'Search Extensions in Marketplace'.
There are a bunch to install!
As you search for each one from the list below, it will give you an option to install it. Install each one in turn:

* C/C++ (by Microsoft)
* C/C++ Extension pack (by Microsoft)
* C/C++ Themes (by Microsoft)
* Markdown Preview Github Styling
* Cortex-debug
* MemoryView
* RTOS Views
* CMake
* CMake Tools
* markdownlint
* Python (by Microsoft)

One last thing: If you ever consider participating in this project, please go to your VS Code settings, type "trailing" into the settings searchbox, and make sure the box is checked for "Files: Trim Trailing Whitespace".

### Windows Terminal App

Go to the Microsoft store and download the "Windows Terminal" free app.
It works great for interacting with WSL2.
It supports multiple terminal windows in different tabs which is handy.

_Fix Me: it's been so long, I don't remember what I needed to do to set up a terminal profile to talk to WSL2. Sorry! It's not critical, but it makes opening a new terminal tab faster.

From this point on, any of the instructions in this document that are executed from a command line will be using a Windows Terminal window that is running Ubuntu/WSL.

### WSL2 Installation

The WSL2 installation process is defined [here](https://learn.microsoft.com/en-us/windows/wsl/install).
Follow those instructions and all will be well.

Open a Windows Terminal window into WSL2.
Get your new WSL2 Linux system up to date by typing:

```bash
$ sudo apt update
$ sudo apt upgrade
```

The first time around, these commands may install a bunch of updates.
You can run this command pair whenever you feel like to keep your Linux up-to-date.

## Linux/WSL2 Software Installation

Before starting the installation process for all of the Linux software, make sure your machine is up to date:

```bash
$ sudo apt update
$ sudo apt upgrade
```

Get your system updated before proceeding.

## Create a Local Bin Directory

This project creates a few special executables to help build the software.
Rather than put these tools in the standard system-wide installation locations,
the build system will place them in a user-specific ~/.local/bin directory.

If that directory does not exist, create it via the following:

```bash
$ mkdir -p ~/.local/bin
```

The standard Ubuntu ~/.profile will automatically add your new .local/bin directory to the PATH variable.
Check your PATH to see if the "~/.local/bin" directory is on it:

```bash
$ echo $PATH|tr ':' '\n'|grep '[.]local'
/home/<your-user-name>/.local/bin
```

If the directory is not on your PATH, log out and log in again so that your '~/.profile' gets re-executed.
Typically, the ~/.profile will only add '~/.local/bin' to your PATH if the directory exists.
If your .profile is not adding ~/.local/bin to your path, edit your .profile to add the following lines:

```bash
# set PATH so it includes user's private bin if it exists
if [ -d "$HOME/.local/bin" ] ; then
    PATH="$HOME/.local/bin:$PATH"
fi
```

Log out and log in again (or type 'source ~/.profile'), and verify that '~/.local.bin' is on your PATH.

### Python

The umod4 system uses Python3 for some utility programs.
Python3 is typically part of Linux distributions, so you probably do not need to install it.
The umod4 project does require using Python virtual enviroments so that it can install various libraries as the build process runs.
To add that capablility, find out what version of python3 is on your system, then do the following, making sure that the version number for the install matches the first two numbers reported by the --version command ("3.10" in the example, below):

```bash
$ python3 --version
Python 3.10.12
$ sudo apt install python3.10-venv
```

### Git

Git is a source-code control system used for managing large projects.
It should already be installed in your WSL2 Ubuntu distribution, but it is harmless to make sure, as shown below.

```bash
$ sudo apt install git
Reading package lists... Done
Building dependency tree... Done
Reading state information... Done
git is already the newest version (1:2.34.1-1ubuntu1.11).
0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.
```

#### Git and Line Endings

The Git repository for umod4 always has Unix-style LF line endings.
Configuring the git setting 'core.autocrlf' to 'false' tells Git to *not* change files to use CRLF-style endings when it checks stuff out onto a Windows machine.
VS Code on Windows operates just fine on LF-style endings so there is no need to add CR characters just because it is a Windows machine.

Type the following in your WSL2 terminal window:

```bash
$ git config --global core.autocrlf false
```

### Host C/C++ Compilers

You will need both C (gcc) and C++ (g++) compilers to build the system.
Run the following two commands in the terminal window.
If either of the compilers is already installed, these requests to install them are harmless.

```bash
$ sudo apt install gcc
$ sudo apt install g++
```

### Build 68HC11 Toolchain

The Gen 1 ECU's processor is a Motorola M68HC11.
The C compiler for the HC11 has not been supported since GCC version 3.4.6, back in the early 2000's.
Fortunately, for the purposes of the building the UM4 software, we don't need a full 68HC11 C compiler, we only need an assembler.
And amazingly enough, a modern GCC 'binutils' package still contains everything we need to target the ECU's 68HC11:

* a 68HC11 assembler
* a linker
* an objcopy utility

The only downside is that a 68HC11 version of the binutils is not something that can just be installed via 'apt', but will need to be built from source code.
Fortunately, this is quite easy:

1) Go to the [GNU Binutils](https://www.gnu.org/software/binutils/) page
2) Find out what the latest version of the binutils is (2.42 at the time this page was written)

Run the commands below, replacing version "2.42" with whatever version you are using.
Or if you are lazy, just copy the commands below to install version 2.42, which is known to work fine.
The commands assume that you would like to put your binutils source in a directory called ~/binutils/binutils-2.42.
This naming convention allows you to support multiple versions of the binutils, should that be useful.

```bash
$ sudo apt install texinfo
$ cd ~
$ mkdir binutils
$ cd binutils
$ wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
$ tar zxvf binutils-2.42.tar.gz
$ mv binutils-2.42 binutils-m68hc11-elf-2.42
$ cd binutils-m68hc11-elf-2.42
$ ./configure --prefix=$HOME/.local --target=m68hc11-elf
$ make
```

The 'make' operation will take a couple minutes.
Assuming that 'make' completed without errors, install the tools and then make sure they ended up in the right place.
You should see a whole bunch of new executable files all relating to the m68hc11 processor:

```bash
$ make install
$ ls -l ~/.local/bin
total 86244
-rwxr-xr-x 1 robin robin 4964128 Aug  7 07:09 m68hc11-elf-addr2line
-rwxr-xr-x 2 robin robin 5150512 Aug  7 07:09 m68hc11-elf-ar
-rwxr-xr-x 2 robin robin 6816200 Aug  7 07:09 m68hc11-elf-as
-rwxr-xr-x 1 robin robin 4911176 Aug  7 07:09 m68hc11-elf-c++filt
-rwxr-xr-x 1 robin robin  125992 Aug  7 07:09 m68hc11-elf-elfedit
-rwxr-xr-x 1 robin robin 5556088 Aug  7 07:09 m68hc11-elf-gprof
-rwxr-xr-x 4 robin robin 8066152 Aug  7 07:09 m68hc11-elf-ld
-rwxr-xr-x 4 robin robin 8066152 Aug  7 07:09 m68hc11-elf-ld.bfd
-rwxr-xr-x 2 robin robin 5030672 Aug  7 07:09 m68hc11-elf-nm
-rwxr-xr-x 2 robin robin 5792040 Aug  7 07:09 m68hc11-elf-objcopy
-rwxr-xr-x 2 robin robin 8331408 Aug  7 07:09 m68hc11-elf-objdump
-rwxr-xr-x 2 robin robin 5150544 Aug  7 07:09 m68hc11-elf-ranlib
-rwxr-xr-x 2 robin robin 4325152 Aug  7 07:09 m68hc11-elf-readelf
-rwxr-xr-x 1 robin robin 4954320 Aug  7 07:09 m68hc11-elf-size
-rwxr-xr-x 1 robin robin 4966904 Aug  7 07:09 m68hc11-elf-strings
-rwxr-xr-x 2 robin robin 5792040 Aug  7 07:09 m68hc11-elf-strip
```

Finally, do a trivial test of the assembler by typing the commands, below.

```bash
$ cd ~
$ m68hc11-elf-as --version
GNU assembler (GNU Binutils) 2.42
Copyright (C) 2024 Free Software Foundation, Inc.
This program is free software; you may redistribute it under the terms of
the GNU General Public License version 3 or later.
This program has absolutely no warranty.
This assembler was configured for a target of `m68hc11-elf'.
```

The important part is that the new assembler knows it has been configured to target "m68hc11-elf", which represents the processor inside the ECU.
If the system was not able to find the m68hc11-elf-as executable, make sure that "~/.local/bin" is on your PATH, as described earlier in this document.

### ARM Cross-Compiler Toolchain

ARM cross compilers are required to build the code that runs on the umod4 circuit board.
To get the Arm cross-compiler installed, start off by downloading an appropriate toolchain from the Arm download page located [here](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).

#### For x86 PC

Assuming that your PC host is an x86 machine capable of running linux/WSL2, scroll down until you see the section called 'x86_64 Linux hosted cross toolchains'.
From the 'AArch32 bare-metal target (arm-none-eabi)' subsection, find the link to download 'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz'.
The version number in the example (13.3.rel1) may have changed since this document was last updated, so just locate the most recent version, whatever it is.
Don't download the file though, just right click the link and select "copy link".

#### For ARM Raspberry Pi 5

If you are developing on a Pi 5, scroll down the ARM download page until you see the section titled 'AArch64 Linux hosted cross toolchains'. You want the version 'AArch32 bare-metal target (arm-none-eabi)', downloaded via the file 'arm-gnu-toolchain-13.3.rel1-aarch64-arm-none-eabi.tar.xz'. As with the x86 instructions, don't actually download the file, but right-click the link and select 'copy link'.

Once the link has been selected for the proper download, we need to prepare a place for the downloaded cross-compilation tools to live.
If you have your own favorite way of doing things like this, do it your way.
Note that if you *do* change where you want to put the tools, you will need to update the umod4 project file 'cmake/toolchains/arm-none-eabi.cmake' to reflect your changes.
Otherwise, this is the recommended way:

```bash
# This assumes that the latest version was named 13.3.rel1:
$ sudo mkdir -p /opt/arm/arm-none-eabi/13.3.rel1
```

The point of this directory structure is to allow multiple versions of the toolchain to live on your system.
The toolchain directory will look like this:

```text
/opt
└── arm
    └── arm-none-eabi
        └── 13.3.rel1

```

In the future, when some new, hypothetical version 14.1 of the tools gets released, you could download that new toolchain beside the current 13.3 directory.
The resulting directory structure would look like this:

```text
/opt
└── arm
    └── arm-none-eabi
        └── 13.3.rel1
        └── 14.1.rel1
```

You will be able switch over to the new tools or switch back to the old ones by just changing the appropriate CMake toolchain file to point at the proper directory.

Now that the toolchain has a place to live, it's time to get it and install it.
Assuming that the link to the toolchain on the ARM download website is still in you copy buffer, you can paste it after the 'wget' to avoid a bunch of typing in the example below.
Make sure you are in the proper directory before downloading the code, then get it using 'wget':

```bash
$ cd /opt/arm/arm-none-eabi/13.3.rel1
$ sudo wget https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz
```

At this point, you should have a very large archive file in your directory:

```bash
$ ls -l
-rw-r--r-- 1 root root 147343268 Jul  3 05:10 arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz
```

Next up is to extract the files from the archive that was downloaded.
One problem (for me) is that every single file in the entire archive is prepended with an annoyingly long initial pathname: 'arm-gnu-toolchain-13.3Rel1-x86_64-arm-none-eabi'.
I suppose it might be useful if you needed to install cross compilers for every single Arm architecture, but this project doesn't need that level of complexity.
To get rid of that long pathname, extract the archive using the following command:

```bash
$ sudo tar xf ./arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz --strip-components 1
```

Once tar completes, the cross-compilation executable tools will be located at /opt/arm/arm-none-eabi/13.3.rel1/bin.
Verify that the new tools are functioning by running gcc directly from its bin directory:

```bash
    $ ./bin/arm-none-eabi-gcc --version
    arm-none-eabi-gcc (Arm GNU Toolchain 13.3.rel1 (Build arm-13.7)) 13.3.1 20231009
    Copyright (C) 2023 Free Software Foundation, Inc.
    This is free software; see the source for copying conditions.
 There is NO
    warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

There is no need to add the cross-compiler's bin directory to your PATH variable.
Instead, this project will use CMake's 'toolchain' mechanism to tell the build system how to find the tools.

#### Updates When Installing a New Version of ARM tools

The file 'projects/umod4/cmake/toolchains/arm-none-eabi.cmake' is set up to use the version of the arm tools that were just installed.
Verify that the variables from that file as shown below match where you downloaded and installed the tools, and that the version number matches:

```cmake
# This explicitly overrides the built-in tools to use a specific version
set(ARM_NONE_EABI_VERSION "13.3.rel1")
set(CROSSCOMPILE_TOOL_PATH "/opt/arm/arm-none-eabi/${ARM_NONE_EABI_VERSION}/bin")
```

### GDB

GDB is the Gnu Debugger.
It will be used to debug the RP2040 code that runs on the umod4 board.
Try executing the new cross-tool GDB as follows, and you will probably see the following error:

```bash
$ cd /opt/arm/arm-none-eabi/bin
$ ./arm-none-eabi-gdb  --version
./arm-none-eabi-gdb: error while loading shared libraries: libncursesw.so.5: cannot open shared object file: No such file or directory
```

FYI, the 'w' version of libncurses (i.e. ''libncursesw.so.5') is the same as libncurses except that it can deal with 'wide' characters, meaning the UTF-8 charset.
To fix this, we need to install libncurses5 as follows:

```bash
sudo apt-get install libncurses5 libncursesw5
```

When using WSL, the libraries get installed in '/usr/lib/x86_64-linux-gnu', not the standard linux location '/usr/lib'.
WSL users will need to add '/usr/lib/x86_64-linux-gnu' to their PATH variable.
It is easiest to modify your .bashrc file to add the following line:

```bash
export PATH=$PATH:/usr/lib/x86_64-linux-gnu
```

Now, GDB should run:

```bash
$ cd /opt/arm/arm-none-eabi/bin
$ ./arm-none-eabi-gdb --version
GNU gdb (Arm GNU Toolchain 13.3.Rel1 (Build arm-13.24)) 14.2.90.20240526-git
Copyright (C) 2023 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
```

### CMake

CMake is used to define a build process to create all the pieces in the entire umod4 system.
It can be installed via the standard Ubuntu Linux mechanisms, as shown below.
Run 'cmake' after it gets installed to prove that it exists:

```bash
$ sudo apt install cmake
$ cmake --version
cmake version 3.22.1

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

### Ninja

Ninja is the low-level build tool used by VS Code and CMake to decide when and how to construct all the umod4 components.
Ninja is not installable in the normal 'apt install ...' fashion.
Instead, it comes as a zip file containing a single binary executable that needs to be stored somewhere.
In this case, we will store ninja in our ~/.local/bin directory that was created earlier in this document.

Use a Windows browser to get to the ninja [download page](https://github.com/ninja-build/ninja/releases)
The download file 'ninja-linux.zip' contains the x86 ninja executable.
Download that file, then open it.
The zip extractor will ask where to put it.
Type '\\wsl$' then hit return.
Remember this '\\wsl$' starting point because this is how Windows can access the WSL filesystem.
Double click the Ubuntu icon and you will see that you are now at directory '/', the root of your WSL filesystem.
From there, click through 'home', your user name, '.local', and finally, 'bin'.
Select bin as the extraction target and then extract.

From your WSL terminal window, type the following:

```bash
$ ll ~/.local/bin
total 276
drwxr-xr-x 2 robin robin   4096 Aug  5 11:43 ./
drwxr-xr-x 4 robin robin   4096 Aug  5 11:43 ../
-rw-r--r-- 1 robin robin 273768 May 11 12:45 ninja
-rw-r--r-- 1 robin robin      0 Aug  5 11:43 ninja:Zone.Identifier
```

You can remove the 'ninja:Zone.Identifier' file, if you see it.
It is trash left over from the Windows zip extraction process.
Run 'chmod' to make sure that the extracted 'ninja' file is executable, then run it as a test:

```bash
$ cd ~/.local/bin
$ chmod +x ninja
$ ./ninja --version
1.12.1
```

Verify that your ~/.local/bin directory is on your PATH:

```bash
$ cd
$ which ninja
/home/robin/.local/bin/ninja
```

If the 'which' command could not find ninja, you have a problem.
See the section about '~/.local/bin' earlier in this document, and make sure that directory is on your PATH.

### Install OpenOCD

OpenOCD is the "Open On-Chip Debugger" software tool.
It is required to debug the EP and WP firmware created by the umod4 project.
OpenOCD has been around forever, but it needs to run a special version for the RP2040 chip.
At some point, the RP2040 support will exist in the Ubuntu distribution and installing OpenOCD will be as simple as "sudo apt install openocd".
For now, it needs to be built from source.

Install source code for OpenOCD, making sure to get the sources from raspberrypi where the RP2040 support is located.
Go to the official Rpi site located [here](https://github.com/raspberrypi/openocd).
Find the name of the default branch, named 'rp2040-v0.12.0' at the time of this writing.
You will need the name of that branch in a couple steps.
Type:

```bash
$ cd ~/projects
$ git clone https://github.com/raspberrypi/openocd.git --recursive --branch rp2040 --depth=1
```

The openocd project is now checked out into directory ~/projects/openocd.
Next, we need to check out the proper branch that contains the RP2040 support (use the branch name you got from the previous step):

```bash
$ cd ~/projects/openocd
$ git checkout -b rp2040-v0.12.0
Switched to a new branch 'rp2040-v0.12.0'
```

Install the developer version of ncurses-5:

```bash
$ sudo apt-get install libncurses5-dev libncursesw5-dev
```

Install a bunch of packages that OpenOCD will need:

```bash
$ sudo apt install libusb-1.0-0 libusb-1.0-0-dev libhidapi-dev libtool texinfo pkg-config
```

Finally, build OpenOCD:

```bash
$ cd ~/projects/openocd
$ ./bootstrap
$ ./configure --enable-ftdi --enable-sysfsgpio --enable-bcm2835gpio --enable-cmsis-dap
$ make
$ sudo make install
$ openocd --version
```

The fun never ends with openocd though.
Now you need to create a file in directory "/etc/udev/rules.d"

The file is named "46-probe.rules".
Use the nano editor in sudo mode to create that file containing the following text:

```text
# Pi Pico CMSIS-DAP USB debug probe
ATTRS{idProduct}=="000c", ATTRS{idVendor}=="2e8a", MODE="666", GROUP="plugdev"
```

Once the file is written, do the following:

```bash
$ sudo udevadm control --reload
$ sudo udevadm trigger
```

You will not need to do that again because the '46-probe.rules' file you created will take care of setting the permissions every time the machine reboots.

## Umod4 Software Packages

At this point, the Linux system is ready to go with all the tools required to build the umod4 project.
Now we need to load up a bunch of software packages that umod4 needs.
These packages need to be loaded into a project directory in a particular fashion so that umod4 can find them.
This process is described below.

### Development Directory Structure

There is a fair amount of software source that needs to be installed, and a some of it needs to know where other parts of it are located.
In addition, the Pico SDK (Software Development Kit) is quite large, at over 600 megabytes.
The SDK could be installed by cmake as sub-piece of the umod4 project, but it is so large that replicating the entire SDK for every project in your sytem that might need it is a bad idea.
A simple way to share a single SDK installation with more than one project is to store a single copy of the SDK at a well-known location in the 'projects' directory, and then just tell the sub-projects that need the SDK where to find it.

Pictorially, we want to end up with a directory structure that looks like this.
Notice that the directory structure is set up so that we can store multiple versions of the SDK, allowing different projects the ability to use different versions:

```text
/home/<your-user-name>/
└── projects/
    ├── FreeRTOS-Kernel
    ├── pico-sdk
    │   ├── 1.5.1
    │   └── 2.0.0
    └── umod4
```

The umod4 build system assumes that this form of directory structure will be used.
You could change this if you really wanted, but it would be a lot of work and is not recommended.

### RPi SDK

The Raspberry Pi RP2040 Software Development Kit (SDK) is a collection of software tools and code libraries that make it easier to work with the RP2040 processor chips that are used by the umod4.

The SDK can be installed in a number of fashions.
For various reasons, we will install it using Git, a source control system.
Using Git makes it easy to update to new versions.

```bash
$ cd ~/projects
$ mkdir pico-sdk
$ cd pico-sdk

# This clones the 'master' branch of the pico-sdk
$ git clone https://github.com/raspberrypi/pico-sdk

# Rename pico-sdk to reflect the SDK branch that we will be checking out:
$ mv pico-sdk 2.0.0

# Tell git that we actually want to use the "2.0.0" tag on the master branch
$ cd 2.0.0
$ git checkout 2.0.0

# Update our branch so that it can do WiFi and Bluetooth
$ git submodule update --init
```

At this point, your 'projects' directory hierarchy should look like this:

```text
projects/
└── pico-sdk
    └── 2.0.0
```

### FreeRTOS

FreeRTOS is a free, Real Time Operating System (RTOS).
The umod4 project uses it on the WiFi Processor (WP).
A special version of FreeRTOS is required for the RP2040 processor used on umod4 because
the RP2040 is a dual core processor, which is still a bit unusual in the FreeRTOS world.

FreeRTOS is a big enough project that it makes more sense to store it in the projects directory like the Pico-SDK, instead of replicating it inside each RP2040 project that might need it.
To get FreeRTOS, type the following commands.
The critical part is to clone the 'smp' branch (via option '-b smp').
This branch adds 'Symmetric Multi Processor' support, which is what is needed for the dual-core processor in the RP2040.

```bash
$ cd ~/projects
$ git clone -b smp https://github.com/FreeRTOS/FreeRTOS-Kernel --recurse-submodules
$ ls -l
total 20
drwxr-xr-x  5 robin robin 4096 Aug  6 09:58 ./
drwxr-x---  7 robin robin 4096 Aug  6 07:02 ../
drwxr-xr-x  7 robin robin 4096 Aug  6 09:58 FreeRTOS-Kernel/
drwxr-xr-x 11 robin robin 4096 Aug  6 09:20 pico-sdk/
```

You should now see a 'FreeRTOS-Kernel' directory beside your 'pico-sdk' directory:

```textr
projects/
  ├── FreeRTOS-Kernel
  └── pico-sdk
      └── 2.0.0
```

## Getting the Umod4 Source Code

It is finally time to get the umod4 code loaded onto your system!

Click on the 'source control' icon from the ribbon on the left side of the VS Code window.
As in the RPi SDK section, select 'clone repository', then 'clone from Github', and type 'mookiedog/umod4' in the search box.
Select the highlighted 'mookiedog/umod4' item from the list.
A window will ask where you want to store the repository.
Like before, navigate to your home directory, and click into the projects directory, then click 'ok'.

When it completes, your 'projects' directory structure should show you a new umod4 directory:

```text
projects
    ├── FreeRTOS-Kernel
    ├── openocd
    ├── pico-sdk
    └── umod4
```

Your VS Code window should show a bunch files.
In fact, if you select the file "BUILDING.md' by clicking it, you will see the file that contains what you are reading right now.

## Building Umod4

It should be clear by now that the umod is not a particularly simple system to get working!
But you are finally ready to try and build it.
In your VS code window that you used to get the umod4 code from Github, hit 'F1' then type 'delete', but don't hit return.
A bunch of selections related to the topic of 'deleting' will appear in a dropdown list.
Click the list item called 'CMake: Delete Cache and Reconfigure'.
After some amount of time, that operation should finish without errors.
It should produce a bunch of messages in the VS Code 'output' window that looks something like this:

```text
[main] Configuring project: umod4
[proc] Executing command: /usr/bin/cmake -DCMAKE_BUILD_TYPE:STRING=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE --no-warn-unused-cli -S/home/robin/projects/umod4 -B/home/robin/projects/umod4/build -G Ninja
[cmake] Not searching for unused variables given on the command line.
[cmake] -- The C compiler identification is GNU 11.4.0
[cmake] -- The CXX compiler identification is GNU 11.4.0
[cmake] -- Detecting C compiler ABI info
[cmake] -- Detecting C compiler ABI info - done
[cmake] -- Check for working C compiler: /usr/bin/cc - skipped
[cmake] -- Detecting C compile features
[cmake] -- Detecting C compile features - done
[cmake] -- Detecting CXX compiler ABI info
[cmake] -- Detecting CXX compiler ABI info - done
[cmake] -- Check for working CXX compiler: /usr/bin/c++ - skipped
[cmake] -- Detecting CXX compile features
[cmake] -- Detecting CXX compile features - done
[cmake] -- Fetching littlefs project...
[cmake] -- ...Fetched littlefs!
[cmake] -- Beginning ExternalProject_Add: tools
[cmake] -- Beginning ExternalProject_Add: ecu
[cmake] -- Beginning ExternalProject_Add: EP
[cmake] -- Beginning ExternalProject_Add: WP
[cmake] -- Configuring done
[cmake] -- Generating done
[cmake] -- Build files have been written to: /home/robin/projects/umod4/build
```

The important part is that the messages end with "Build files have been written to: ..." which means, no errors.

Assuming that the system configured without errors, you finally get to build the umod4 project.
In the VS Code window, hit key 'F7' to build everything.
The VS Code output window will get tons of messages as everything runs.
If it all goes according to plan, you will see the following down at the very bottom of all those messages:

```text
[build] Build finished with exit code 0
```

The exit code of 0 means **"All Is Good"**.
A non-zero exit code means that some part of the build failed.
If you get a non-zero exit code, scroll back to the top of the message output window, then scan down through all the output until you see the first error message appear.
Then, fix the error.
I know, I know, maybe not so simple...

**Important:** the CMake process is designed to put anything that gets generated or created by the build process into a directory called 'build', located under the main "projects/umod4" directory.
This means that it is *always* safe to delete the entire contents of the build directory because anything in that directory can be re-created automatically.
Sometimes, things can get out of sync in the CMake build process.
It usually happens after making changes to one of the CMakeLists.txt files in the system.
If things are acting weird, the nuclear option to get back on track is to:

1) delete the entire 'build' directory
1) hit "F1", then type "CMake: delete cache and reconfigure"
1) hit "F7" to rebuild everything

## Interlude

Now would be a good time to take a deep breath and look around the system.
Looking inside the main directory, you will see a few subdirectories containing various parts of the project:

* **ecu**: This sub-project generates the special **UM4** data-logging software that will run inside the Aprilia ECU
* **EP**: the **E**prom **P**rocessor.
The EP is a Raspberry Pi RP2040 processor that pretends to be the EPROM that plugs into the ECU EPROM socket.
It is responsible for creating an EPROM image that it will feed to the 68HC11 processor in the ECU.
It is also responsible for accepting the ECU data stream that is generated by the data-logging software and forwarding it to the WP.
* **WP**: the **W**ireless **P**rocessor.
The WP is a Raspberry Pi Pico-W board, also containing an RP2040 processor like the EP.
The WP takes the incoming ECU data stream from the EP and logs it to a micro SD card.
Simultaneously, it logs position and velocity data from an on-board GPS module.
In the future, the WP will be extended to dump logs off the bike via WiFi, and add features like EPROM image selection.
* **eprom_lib**: This sub-project contains information about many of the stock EPROMs used by the Gen1 ECU.
The information takes the form of JSON documents because that makes them fairly human-readable, but easily processed into machine-readable BSON documents that get included into the EP build.
From a rider's standpoint, it means that every umod4 board can contain many, many EPROM images.
Things are set up so that the actual image that is presented to the ECU can be combined from more than one EPROM.
Typically, one would combine the UM4 ECU logging firmware with the maps from some other EPROM.
This effectively converts an any RP58-compatible stock EPROM into a data-logging EPROM.
Of course, the system also supports running any .bin file for whatever EPROM you happen to have laying around.

## Running Umod4

If by some miracle the system built successfully, the next step would be to run it by using the debugger to flash the software on a umod4 circuit board.
But for that, you would need an Ultramod4 circuit board and an ECU that has been modified to accept it.
And that's where things will stop for a while because currently, I am the only one in the world who has working umod4 hardware.

In the meantime, thanks for checking out this project.

## Debugging

*For documentation purposes, here's some of the next steps.
From here on, you would need a umod4 PCB and a slightly modified ECU.*

The umod4 project contains a '.vscode/launch.json' file which tells VS Code how to work with the debugger hardware.

Sadly, there is a bug in either CMake or VS Code or both which results in VS Code not knowing how to find the executables produced by the CMake 'ExternalProject_Add()' comamnds.
Hopefully, this will get fixed at some point.
But for now, the 'launch.json' file is modified so that it creates an explicit command to flash and debug the EP and WP portions of the umod4 project.

### Getting WSL To See a Debug Probe

Ownership of USB devices is an issue for WSL.
When you plug a USB device into a Windows machine, Windows owns it by default, as opposed to WSL.
However, there is a mechanism that lets you tell Windows to give control of a specific USB device to WSL.

Plug your [Raspberry Pi Pico Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html) into a USB port on your Windows machine.

From a Windows powershell or cmd prompt, type:

```text
winget install --interactive --exact dorssel.usbipd-win
```

It will download and run an installer.
Do what the installer says.

After the installer completes, open a powershell 'administrator' window, then type "usbipd list" as shown.
You will get back something like this, obviously depending on the USB devices that are attached to your own machine:

```text
PS C:\Users\robin> usbipd list
Connected:
BUSID  VID:PID    DEVICE                                                        STATE
6-2    062a:4102  USB Input Device                                              Not shared
8-2    2e8a:000c  CMSIS-DAP v2 Interface, USB Serial Device (COM5)              Shared
8-3    046a:010d  USB Input Device                                              Not shared
8-4    2357:012e  TP-Link Wireless USB Adapter                                  Not shared
8-5    0bda:2550  Realtek Bluetooth 5.1 Adapter                                 Not shared

Persisted:
GUID                                  DEVICE
```

Locate the proper device, the one with "CMSIS-DAP" in its name.
In this case, its busID (its USB address info) is 8-2.
It will be different on your system.
Make a note of the busID, then do the following in the same powershell administrator window:

```text
PS C:\Users\robin> usbipd bind --busid 8-2 (or whatever your own system's busID was for the CMSIS-DAP device)
```

This is a one-time command to tell Windows that it is allowed to connect that device to WSL at a later point in time.

When it is actually time to connect a debugger, you will need to open a powershell window (doesn't need to be an adminstrator windown), and type the 'list' and 'attach' commands as shown below:

```text
PS C:\Users\robin> usbipd list
Connected:
BUSID  VID:PID    DEVICE                                                        STATE
6-2    062a:4102  USB Input Device                                              Not shared
8-2    2e8a:000c  CMSIS-DAP v2 Interface, USB Serial Device (COM5)              Shared
8-3    046a:010d  USB Input Device                                              Not shared
8-4    2357:012e  TP-Link Wireless USB Adapter                                  Not shared
8-5    0bda:2550  Realtek Bluetooth 5.1 Adapter                                 Not shared

Persisted:
GUID                                  DEVICE
PS C:\Users\robin>usbipd attach --wsl --busid 8-2
```

You need to do the list command in case the busID changed, then the attach using that busID.
Sadly, each time you restart WSL, you will need to do this last 'attach' step to request that Windows
transfers control of the debugger dongle to WSL.
It's a bit annoying to have to keep reconnecting the USB dongles each time you restart WSL, but that's life.
Unlike the bad old days, at least WSL can access USB devices now!

### Preparing to Launch

The "launch.json" file needs to know where to find the Pico SDK so that it can access the RP2040 chip's peripheral register definitions.
Put this definition in your .bash_rc:

```bash
export PICO_SDK_PATH=/home/<your-user-name>/projects/pico-sdk/<version-you-are-using>
```

That last definition assumes that you used all the defaults and didn't put 'projects' somewhere else, or with a different name, and that you put some specific version of the SDK under 'projects/pico-sdk' as suggested.

### Starting the Debugger

At the moment, there is a bug in VS Code CMake that does not allow executables from subprojects to be found, meaning that you can't select them as part of starting the debugger in the normal fashion.
To get around that, when you are ready to debug, type "F1", then "Debug: Select And Start Debugging".
That will bring up a window of all the launch configurations as menu choices.
Click the first one, named '*** EP: Launch CMSIS-DAP'.

And finally, you should see something like this:

![image](doc/images/debugging.jpg)

There you are: the EP code has been flashed into the EP processor on the umod4 board.
The debugger has stopped execution at the first line of main() (that's why it is highlighted in yellow).
The EP processor is ready to load an EPROM image and start feeding those instructions to the ECU when when you hit "F5".

Congratulations if you made it this far.

I know it was not a simple process to replicate, or to create in the first place.
I only got it working due to tons of people on the internet who ran into all the same problems as I did, but somehow were smart enough to figure out the issues, and more critically, to document their successes in places where DuckDuckGo could find them for me.

## Whats Next

Short version: For all that has happened so far, there is still a long way to go.

### ECU Firmware

At the moment, the ECU data logging firmware logs a certain amount of stuff as the bike runs.
I would really like to mod the firmware so that it logs the exact table entries it is using as it performs its calculations.
That way, if the engine does something you don't like, you would have the ability to look back and see what part of the maps it was using when the bad behavior happened.

### EP Firmware

The EP firmware is in good shape.
It needs a two-way communication mechanism to be created so that the WP can send it commands.
These commands would be used for things like EPROM selection or map updates.
Right now, that gets done at build time.
Ultimately, it would be better if the WiFi/Bluetooth capabilities of the WP could be extended to allow a user to use their phone to pick an EPROM to run, or a set of maps to use before a ride.

### WP Firmware

The WP is an experimental work in progress.
It has minimal capabilities right now.
It can configure the GPS and log the GPS position and velocity data.
Or at least, it used to be able to do so.
I have not debugged SD card code yet since it ran on the umod version 3.
I am in the process of switching over from using FAT-formatted micro SD cards to using LittleFS.
LittleFS is designed for flash file systems, and is significantly more reliable than FAT.
I need to do some performance testing before making the change.

The biggest issue is that I'm not sure that the RPi Pico-W board I am using as the WP will have enough WiFi horsepower to do all the wifi work that I am imagining.
An obvious solution would be to use a more powerful board like a Pi Zero 2W, but boards like that draw so much power that it worries me that the ECU power supply would not be able to handle it.
The initial goal will be to see if the current Pico-W design can get the job done, even if it is kind of slow.
If so, the ECU power supply will be fine.
Time (and experiments) will tell.

### EPROM Library

I would like to get the .dsc files that describe the various EPROMs that exist in the world up to date, as far as possible.
If nothing else, the .dsc files will be a useful historical record for people I see googling "what is this XYZ Aprilia Eprom?"

### Umod4 PCB Hardware

Another big area to work on.
The good news is that I have created version 1 PCBs, and with a couple of cuts and jumpers, they do work.
The EP hardware has been proven to work perfectly, fooling the ECU's processor into thinking that it is just another EPROM in the ECU's EPROM socket.
The ECU has no idea that it's a very special "EPROM", at that!
I'm proud of that.

The less good news is that in the time that has elapsed since my first boards were made, I'm sure that some of the parts on the board have gone out of stock, or worse yet, become obsolete at the fab house I use to make the boards.
It will be a bunch of tedious work to go through every part on the board, verify its current availability, spec a new part if not, and finally get a new set of boards built.

I was holding off on making any more boards until I knew if the WP would be powerful enough.
If not, it will take a major board redesign to spec and design in a more powerful wireless processor.
I'm running into physical space limitations too, so just because I can find a more powerful wireless processor does not mean that it will fit on the board.
So I would like to write some code and do some experiments to see if the Pico W is up to the task.

Another big job is that the board's CAD design should get ported from using Eagle to using something like Kicad.
I've used Eagle forever, but it is not supported anymore and it is not open-source.
In some regards, I don't care because my old version of Eagle still works great and produces files that the fab houses accept and process correctly.
But if this project is still around in another 20 years, the design really should get migrated into Kicad at some point.
That will be another ton of work, ...for some other day.

## Final Thoughts

So that's where things stand. I will probably always be working on some aspect of this project because that's what "fun" looks like to me.
Weird, I know, but fun comes in many forms.

![image](doc/images/proudly-made-1.jpg)
