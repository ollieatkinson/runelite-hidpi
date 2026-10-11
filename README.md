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

The HiDPI shortcut follows the AppImage location in the normal Jagex desktop
entry on each launch, so updates that replace or relocate the launcher do not
require setup again. The path supplied to `--setup` is a fallback when that
entry is unavailable.

Set KDE's X11 scaling to **Apply scaling themselves** and enable RuneLite's
custom window chrome.

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
