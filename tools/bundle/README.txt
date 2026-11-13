umod4 tools: the server and the data visualizer
================================================
Bundle: {VERSION}

This folder holds the two programs that run on your PC (and, in the
"firmware" folder, the firmware files for the umod4 itself):

  * the SERVER finds your umod4 on your home network, downloads its ride logs,
    and installs new firmware on it over WiFi
  * the VISUALIZER turns a ride log into graphs and a map

The only thing you need to install first is a small tool called uv (STEP 1).
Everything else downloads automatically the first time you run a program.


STEP 1 - Install uv (one-time)
-------------------------------

uv is a small, standalone tool installer. It is NOT Python and does not
touch any Python already on your machine.

Windows (PowerShell - a normal, non-administrator PowerShell window is
fine, no "Run as Administrator" needed):
    powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"

Linux:
    curl -LsSf https://astral.sh/uv/install.sh | sh

After installing, close and reopen your terminal, then confirm it worked:
    uv --version


STEP 2 - Run the server
------------------------

Windows:  double-click  run_server.cmd
Linux:    ./run_server.sh        (from a terminal in this folder)

The first run downloads the packages the server needs, which takes a few
minutes. This only happens once. If this machine has no Python 3.12, uv also
downloads one for you; it is kept in uv's own folder and does not touch any
other Python you may have.

A window titled "umod4 Server" opens with a list of devices. Power up your
umod4 with WiFi in range, and it appears in the list within a few seconds.

On Windows, expect two warnings the first time:
  * "The publisher could not be verified": Windows says this about any program
    downloaded from the internet. Click Run. (It appears again for run_viz.cmd.)
  * Windows may ask whether to allow the server to use the network. Choose
    Allow. If your umod4 never shows up in the list, a firewall blocking UDP
    port 54549 is a likely cause.

The server keeps its database and the logs it downloads here:
    Windows:  %APPDATA%\umod4_server
    Linux:    ~/.umod4_server


STEP 3 - Try the visualizer on the sample log
-----------------------------------------------

Windows:  double-click  run_viz.cmd  (then open the sample log with the File menu),
          or drag  tools\logtools\test_data\sample_ride.um4  onto run_viz.cmd
Linux:    ./run_viz.sh tools/logtools/test_data/sample_ride.um4

(sample_ride.um4 is a 23-minute motorcycle ride with GPS.)

The first run downloads the visualizer's packages (mostly Qt/PySide6,
roughly 250-300MB). This only happens once.

A window opens and asks a couple of quick questions before showing any data,
because a raw log file has to be converted first:

1. "Would you like to convert it to HDF5 format?" -> click Yes.
2. A save dialog asks where to save the converted file. The suggested name and
   location (same folder, same name, .h5 extension) is correct - just click Save.
3. A short "Converting..." message appears - click OK and wait a few seconds.

After that, the visualizer loads automatically and you should see graphs of
motorcycle data: RPM, throttle position, spark timing, GPS speed, and more.
If you open the same log again later, it skips straight to loading, since the
converted .h5 file now sits next to the .um4 file.

For more on using the visualizer, see:
  https://github.com/mookiedog/umod4/blob/main/tools/README_Visualizer_User_Guide.md
  https://github.com/mookiedog/umod4/blob/main/tools/README_Visualizer.md


Project home: https://github.com/mookiedog/umod4


STEP 4 - Update the firmware on your umod4 (only when you need to)
--------------------------------------------------------------------

{FIRMWARE_SECTION}


UPGRADING TO A NEWER VERSION
------------------------------

1. Close the umod4 server and the visualizer if they are running.
   (Only one server can run at a time, so an old one would block the new one.)
2. Unzip the new zip. It makes a new folder with the version in its name,
   next to your old one. Do not unzip it over the old folder.
3. Start the server from the new folder (run_server.cmd or run_server.sh).
4. When you are happy with it, delete the old folder. Your device list and
   logs are not stored there (see STEP 2), so nothing is lost.

You do not need to install uv again.


CLEANING UP / REMOVING THIS LATER
------------------------------------

If you want to get rid of this afterward, there are a few layers to it,
depending on how thorough you want to be.

1. Delete this extracted folder.
   This removes the programs and the packages downloaded just for them.
   The server's own data (your device list and the logs it downloaded) is NOT
   in this folder: it is in the folder named in STEP 2. Delete that too if you
   no longer want those logs.

2. Reclaim the shared package cache (roughly 800MB).
   The first time you (or any uv-based tool) ran something on this machine,
   uv downloaded and cached the underlying packages (Qt, numpy, etc.) in a
   shared location separate from this folder, so that future updates - or
   other uv-based tools - don't have to re-download them every time.
   Deleting the folder in step 1 does NOT remove this cache. To reclaim that
   disk space:
       uv cache clean
   Note: this clears uv's cache for your whole machine, not just this folder.
   If you ever use uv for something else afterward, it will simply be rebuilt
   as needed - nothing is lost by clearing it now.

3. Remove uv itself (optional - most people can stop at step 2).
   uv is a small, separate tool, not part of umod4, and steps 1-2 don't touch
   it. To remove it completely:

   Linux:
       rm ~/.local/bin/uv ~/.local/bin/uvx

   Windows (PowerShell):
       Remove-Item "$env:USERPROFILE\.local\bin\uv.exe","$env:USERPROFILE\.local\bin\uvx.exe"

   uv's installer also added a line to your shell startup file (Linux) or a
   per-user PATH entry (Windows) so "uv" could be found from any terminal.
   Removing that is optional cleanup - a leftover PATH entry pointing at a
   deleted file is harmless - but if you want it gone too: on Linux look for a
   reference to ".local/bin" in your shell's startup file (e.g. ~/.bashrc or
   ~/.profile); on Windows check your user Environment Variables settings.
