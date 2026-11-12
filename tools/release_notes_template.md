# umod4 {VERSION}

Released {DATE} from commit `{COMMIT}`.
{REHEARSAL_BANNER}
## What changed

_(Write this by hand: what is new or fixed, and anything a rider must do.)_

## Installing the tools

1. If it doesn't already exist, make a folder for the tools, for example `C:\umod4\tools`.
   (On Linux or WSL, for example `~/umod4/tools`.)
   **The folder should NOT be under Documents, Desktop, or any other OneDrive backed-up folder (see the note below).**
   Use the same folder for every future version of these tools.
2. Unzip `umod4-tools-{VERSION}.zip` into it.
   In Windows, right-click the zip file and choose Extract All.
   Replace the suggested destination with `C:\umod4\tools` (or whatever you chose in step 1).
   You should end up with `C:\umod4\tools\umod4-tools-{VERSION}\`.
   (On Linux or WSL, unzip the file into your tools folder.)
3. Open that folder and read `README.txt`.
   It explains everything you need to do.

The method described above ensures that newer versions of the tools unzip beside this one.
Old versions can stay until you are happy with the new one, then you can simply delete the old version's folder.
**Your downloaded ride logs are not kept in the tools folder**, so deleting an old version of the tools loses nothing.

**Why the tools folder should stay out of OneDrive and other backup services:**
The first time you run one of the tools, it downloads a few hundred MB of Python packages into the tools folder.
There is no point in backing up these packages.
Everything inside `umod4\tools` can be downloaded again at any time.

## Updating the firmware

To update the firmware on a umod4 you already have, use the server's **Reflash WP** and **Reflash EP** buttons, located under the **Manage Device** tab.

A completely blank WP needs its firmware installed differently: see GETTING_STARTED.md below.

## Tested

_(Write this by hand: what passed, and "Not yet ridden with this build." if that is still true. Keep it short.)_

## Files

| File | What it is | Size | Build time | Compared with {PREVIOUS_VERSION} |
| --- | --- | --- | --- | --- |
{FILES_TABLE_ROWS}

Checksums are in `SHA256SUMS` and, with the build times, in `release.json`.
A firmware file marked **unchanged** is the very same file as in the previous release.
If that release is already on your umod4, there is nothing to reflash for that part.

## Useful links

To build and install a umod4 board, follow [GETTING_STARTED.md](https://github.com/mookiedog/umod4/blob/main/GETTING_STARTED.md).

For more instructions on using a umod4 and the tools in this release, see [USER_GUIDE.md](https://github.com/mookiedog/umod4/blob/main/USER_GUIDE.md).

Found a problem? Please [open an issue](https://github.com/mookiedog/umod4/issues).
