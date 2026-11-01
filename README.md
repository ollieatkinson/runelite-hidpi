# RuneLite HiDPI

Whole-window scaling for RuneLite on KDE Wayland. Tested on CachyOS at 125%.

RuneLite renders at 2× inside a patched Xwayland server; KDE downsamples it to
your desktop scale. The game, sidebar and title bar scale together.

Uses the stock Linux Jagex Launcher and RuneLite, sharing your account and
`~/.runelite` settings.

## Install

Install RuneLite through Jagex Launcher first.

```sh
sudo pacman -S --needed base-devel git
git clone https://github.com/ollieatkinson/runelite-hidpi.git
cd runelite-hidpi
makepkg -si
runelite-hidpi --setup /path/to/Jagex.AppImage
```

Quit Jagex Launcher, then open **Jagex Launcher (RuneLite HiDPI)** and click Play.
Use **Jagex Launcher** for the normal launch route.

Set KDE's X11 scaling to **Apply scaling themselves** and enable RuneLite's
custom window chrome.

## Local bicubic experiment

Close the HiDPI client and launcher, then run the locally built prototype:

```sh
RUNELITE_HIDPI_BICUBIC=1 ./.local/filter-test/bin/runelite-hidpi
```

This uses Pixman's cubic filter on the CPU, with Xwayland's shared-memory buffer
helpers. The backing stays at 2× to match Java's render scale; the output uses the
monitor's reported fractional scale, updating when the window moves between monitors.
Source pixel layout is checked against the root visual, including RGB/BGR and alpha.
The output uses standard 8-bit RGB/ARGB shared-memory buffers (not an HDR pipeline).
Requires the fractional-scale/viewporter protocols; unsupported configurations
retain compositor scaling. GPU readback and CPU
filtering may reduce gameplay performance; this is a visual test, not a GPU
optimization. Window controls and sizing use the existing bridge.

There is no filter-selection CLI or running-instance filter check. To return to
the original path, close the client and launcher and launch without the environment
variable. The variable is read when the private Xwayland starts.

## Rough edges

- One RuneLite client per HiDPI launch.
- Window size is remembered; position isn't.
- “Keep game size” requires an unmaximized window. KDE may move it when sidebar
  expansion would put it past the screen edge.
- Closing the outer window through KDE can bypass RuneLite's exit confirmation.
  Use RuneLite's close button.
- Dialogs and menus are confined to the outer window. Clipboard, tray behaviour
  and mixed-scale monitors haven't been fully checked.
- Resizing may briefly flicker. Gameplay performance hasn't been benchmarked.

## Update

```sh
git pull --ff-only
makepkg -fsi
```

Restart the HiDPI client and launcher after updating.

## Remove

```sh
runelite-hidpi --undo
sudo pacman -R runelite-hidpi
```

Config: `~/.config/runelite-hidpi/`.
Saved sizes and logs: `~/.local/state/runelite-hidpi/`.
Don't share `launcher-private.log`; it may contain account information.
