# umod4 {VERSION}

Released {DATE} from commit `{COMMIT}`.
{REHEARSAL_BANNER}
## What changed

_(Write this by hand: what is new or fixed, and anything a rider must do.)_

## Installing

1. If it doesn't already exist, make a folder for the releases, for example `C:\umod4\releases`.
   (On Linux or WSL, for example `~/umod4/releases`.)
   **The folder should NOT be under Documents, Desktop, or any other OneDrive backed-up folder (see the note below).**
   Use the same folder for every future version.
2. Unzip `umod4-{VERSION}.zip` into it.
   In Windows, right-click the zip file and choose Extract All.
   Replace the suggested destination with `C:\umod4\releases` (or whatever you chose in step 1).
   You should end up with `C:\umod4\releases\umod4-{VERSION}\`.
   (On Linux or WSL, unzip the file into your releases folder.)
3. Open that folder and read `README.txt`.
   It explains everything you need to do.

Windows may warn that "the publisher could not be verified" when you start `run_server.cmd` or `run_viz.cmd`.
This is expected for programs downloaded from the internet: click **Run**.

The method described above ensures that newer versions unzip beside this one.
Old versions can stay until you are happy with the new one, then you can simply delete the old version's folder.
**Your downloaded ride logs are not kept in the releases folder**, so deleting an old version loses nothing.

**Why the releases folder should stay out of OneDrive and other backup services:**
The first time you run one of the programs, it downloads a few hundred MB of Python packages into the release's folder.
There is no point in backing up these packages.
Everything inside `umod4\releases` can be downloaded again at any time.

## Updating the firmware

The firmware files are in the `firmware` folder of the unzipped release.
To update the firmware on a umod4 you already have, use the server's **Reflash WP** and **Reflash EP** buttons, located under the **Manage Device** tab, and choose the file from that `firmware` folder.

A completely blank WP needs its firmware installed differently: see GETTING_STARTED.md below.

## Tested

_(Write this by hand: what passed, and "Not yet ridden with this build." if that is still true. Keep it short.)_

## Files

| File | What it is | Size | Build time | Compared with {PREVIOUS_VERSION} |
| --- | --- | --- | --- | --- |
{FILES_TABLE_ROWS}

**Most people need only `umod4-{VERSION}.zip`: the firmware files are inside it, in the `firmware` folder.**
The other files are the same files, listed separately for anyone who needs just one of them.

Checksums are in `SHA256SUMS` and, with the build times, in `release.json`.
A firmware file marked **unchanged** is the very same file as in the previous release.
If that release is already on your umod4, there is nothing to reflash for that part.

## Useful links

To build and install a umod4 board, follow [GETTING_STARTED.md](https://github.com/mookiedog/umod4/blob/main/GETTING_STARTED.md).

For more instructions on using a umod4 and the tools in this release, see [USER_GUIDE.md](https://github.com/mookiedog/umod4/blob/main/USER_GUIDE.md).

Found a problem? Please [open an issue](https://github.com/mookiedog/umod4/issues).
