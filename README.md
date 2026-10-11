# TV Remote for Flipper Zero

A standalone Flipper application for Unleashed firmware. It turns captured `.ir`
files into a small on-device TV remote: choose an IR file, select a remote button,
and map it to one of the signals in that file. Both decoded and raw IR signals
are supported.

## Local build setup (Windows)

The project uses a local Python environment and keeps its Unleashed SDK/toolchain
inside `.ufbt`:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade ufbt
.\build.ps1 update --index-url=https://up.unleashedflip.com/directory.json --channel=dev
.\build.ps1
```

The generated `.fap` is in `dist`. With your Flipper connected over USB, launch
the app with `.\build.ps1 launch`. Keep using `build.ps1` for SDK commands so it
uses the project-local Unleashed SDK rather than the stock firmware SDK.

## Use

1. Save or copy captured remote files (`.ir`) into `/ext/infrared` on the Flipper.
2. Launch **TV Remote**. The horizontal start screen offers **New Remote** and
   **Saved Remotes**; choose with Left/Right and press OK.
3. For a new remote, choose a captured `.ir` file. Saved Remotes lists files
   that have a saved profile, including profiles with no mapped buttons yet.
4. In the portrait remote view, navigate to a button and hold OK to select one
   of that file's recorded signals. Press OK on a mapped button to transmit it.
   Hold Back to save the remote profile; Back returns to the previous list.
5. On the home screen, hold OK to open **Choose Background**. Use Up/Down to
   select an image and press OK to apply it. Press Back to leave the selector
   without changing the background. Your choice is remembered between launches.

The selected 64x128 image is shown behind the portrait remote controls. The
button faces remain opaque for readability. Four backgrounds are included:
Naruto, Bleach, Death Note, and Hunter x Hunter.

Mappings are saved in `/ext/apps_data/tv_remote` and restored per `.ir` filename.
Saving creates a profile even before any buttons are mapped, and mappings are
also persisted as you assign them.
The app scans `.ir` files directly inside `/ext/infrared` (not nested folders)
case-insensitively, sorts them by name, and displays up to 32 files and 64
signals per remote. Hold a direction to repeat through the file and signal lists.
Lists reserve a separate footer strip for short control hints and status messages.

The remote uses a generic TV layout with Power, Mute, Volume +/-, Channel +/-,
Up/Down/Left/Right, OK, Back, and a dedicated Netflix shortcut. Map Netflix to
the Netflix command from your captured remote file; this sends the TV's IR
shortcut and does not run Netflix on the Flipper itself. Buttons without a
mapping are shown with a small marker.

The portrait remote places the D-pad controls in a directional cluster and
keeps Power, Mute, Volume, Channel, Back, and Netflix separate to avoid
overlapping controls on the Flipper's narrow rotated canvas.

## Background assets

The 64x128 PNG source images are in `backgrounds/themes`: `naruto1.png`,
`bleach1.png`, `deathnote1.png`, and `hxh1.png`. Their 1-bit bitmap data is
embedded in [`backgrounds/themes/theme_bitmaps.h`](backgrounds/themes/theme_bitmaps.h)
and compiled into the app. You do not need to copy the PNGs separately to the
Flipper.

## Project files

- `tv_remote.c` handles input, IR file access, transmission, and saved mappings.
- `tv_remote_view.c` draws the remote and selection screens.
- `tv_remote.h` contains shared app state and UI definitions.
