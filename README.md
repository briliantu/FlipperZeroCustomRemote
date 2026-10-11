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
2. Launch **TV Remote** from the Applications menu.
3. Choose a remote file with Up/Down and press OK.
4. Move around the on-screen button grid with the directional pad.
5. Hold OK on a button to choose a signal from the file. Press OK to transmit
   the mapped signal; use Back to return to the file list.

Mappings are saved in `/ext/apps_data/tv_remote` and restored per `.ir` filename.
The app scans `.ir` files directly inside `/ext/infrared` (not nested folders)
case-insensitively, sorts them by name, and displays up to 32 files and 64
signals per remote. Hold a direction to repeat through the file and signal lists.

The remote uses a generic TV layout with Power, Mute, Volume +/-, Channel +/-,
Up/Down/Left/Right, OK, Back, and a dedicated Netflix shortcut. Map Netflix to
the Netflix command from your captured remote file; this sends the TV's IR
shortcut and does not run Netflix on the Flipper itself. Buttons without a
mapping are shown with a small marker.

## Project files

- `tv_remote.c` handles input, IR file access, transmission, and saved mappings.
- `tv_remote_view.c` draws the remote and selection screens.
- `tv_remote.h` contains shared app state and UI definitions.
