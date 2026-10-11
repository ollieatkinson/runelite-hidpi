# RuneLite HiDPI

Whole-window scaling for RuneLite on KDE Wayland. Tested on CachyOS at 125%.

RuneLite renders at 2× inside a patched Xwayland server; KDE downsamples it to
your desktop scale. The game, sidebar and title bar scale together.

Uses the stock Linux Jagex Launcher and RuneLite, sharing your account and
`~/.runelite` settings.

Text copy/paste works between RuneLite and the desktop using Xwayland's
[upstream clipboard bridge](https://gitlab.freedesktop.org/xorg/xserver/-/merge_requests/2139),
enabled with `-clipboard`. The package uses Xwayland 26.0.99.903 (26.1 RC3).

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

## Rough edges

- One RuneLite client per HiDPI launch.
- Window size is remembered; position isn't.
- “Keep game size” requires an unmaximized window. KDE may move it when sidebar
  expansion would put it past the screen edge.
- Closing the outer window through KDE can bypass RuneLite's exit confirmation.
  Use RuneLite's close button.
- Dialogs and menus are confined to the outer window. System tray icons aren't
  supported; mixed-scale monitors haven't been checked.
- Large clipboard transfers and images haven't been verified. The upstream
  bridge doesn't support incremental transfers yet.
- Login-screen performance matches native in our measurements. Gameplay
  performance hasn't been benchmarked.

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
