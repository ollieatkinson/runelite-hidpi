# RuneLite HiDPI

Whole-window scaling for RuneLite on KDE Wayland. Tested on CachyOS at 125%.

RuneLite renders at 2× inside a patched Xwayland server; KDE downsamples it to
your desktop scale. The game, sidebar and title bar scale together.

Uses the stock Linux Jagex Launcher and RuneLite, sharing your account and
`~/.runelite` settings.

Text copy/paste works between RuneLite and the desktop using Xwayland's
[upstream clipboard bridge](https://gitlab.freedesktop.org/xorg/xserver/-/merge_requests/2139),
enabled with `-clipboard`. The package uses Xwayland 26.0.99.903 (26.1 RC3).

![RuneLite on KDE Wayland with xBR UI scaling and a 120 FPS counter](docs/images/runelite-hidpi-xbr.png)

*GPU bicubic presentation with RuneLite’s xBR UI scaling, running at 120 FPS.*

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

An optional GPU path uses the same Pixman convolution weights in two shader
passes, preserving the 2× Java backing and monitor-sized output without reading
each frame back to the CPU:

```sh
RUNELITE_HIDPI_BICUBIC=1 RUNELITE_HIDPI_BICUBIC_GPU=1 runelite-hidpi
```

Build and install the updated package first; the earlier local prototype does
not include this path. GPU presentation requires Glamor, DMA-BUF buffers and
implicit synchronization support. Unsupported configurations retain CPU bicubic.
Presentation buffers are reused only after the compositor releases them.
To check activation, look for `RuneLite filter bicubic GPU:` in
`~/.local/state/runelite-hidpi/server.log`.

There is no filter-selection CLI. To return to
the original path, close the client and launcher and launch without the environment
variable. The variable is read when the private Xwayland starts.

## Development

`makepkg` installs the build dependencies listed in `PKGBUILD` with `-s`.
The filters compile as separate C files; edit `src/bicubic.vert.glsl` and
`src/bicubic.frag.glsl` for shader changes. Meson embeds the GLSL using `awk`
(`gawk` is a build dependency), so no shader files need loading at runtime.
The launcher's KWin script lives in `src/host-window.js` and is installed beside
the private Xwayland binary.

There are no handwritten Python scripts. Meson still requires Python as part
of its own build tooling.

## Rough edges

- The client can briefly flicker black during startup.
- One RuneLite client per HiDPI launch.
- Window size is remembered; position isn't.
- “Keep game size” requires an unmaximized window. Sidebar resizing keeps the
  game in place; the sidebar can extend past the screen edge if there isn't room.
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
