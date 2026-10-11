# RuneLite HiDPI for KDE Wayland

An experimental CachyOS/Arch package that scales the **entire RuneLite window**:
sidebar, game, text and custom title bar. Native Java renders at 2× into a private
Xwayland; KDE presents it at your desktop scale, such as 125% or 150%.

It launches your installed Jagex Launcher and stock RuneLite, sharing your normal
`~/.runelite` settings, plugins, screenshots and account session. Neither AppImage
nor the system Xwayland is replaced. Stretched Mode is not required.

## Try it

Requires **KDE Wayland on Arch/CachyOS**, a working native Linux Jagex Launcher,
and its installed RuneLite. Quit Jagex Launcher before switching launch routes;
existing RuneLite clients can stay open.

```bash
sudo pacman -S --needed base-devel git
git clone https://github.com/ollieatkinson/runelite-hidpi.git
cd runelite-hidpi
makepkg -si
runelite-hidpi --setup "$HOME/Applications/jagex-launcher-beta-linux-x86_64.AppImage"
```

Adjust the last path to your actual Jagex AppImage. This repository is currently
private, so cloning requires access. `makepkg` installs declared dependencies and
builds the pinned Xwayland source with a checked patch. No pip, virtualenv or
Python project scripts are used; Meson is managed by pacman.

Open **Jagex Launcher (RuneLite HiDPI)** in the application menu, select RuneLite
and click Play. **Jagex Launcher (native)** gives the ordinary launch route.
The launcher-owned shortcut may also appear; our independent shortcuts survive
its rewrites. Updates take effect after closing the HiDPI client and launcher.

For whole-window fractional scaling, KDE's X11 setting should be **Apply scaling
themselves**. Check your current setting first: system scaling can add blur or
another scaling pass. The installer never changes desktop settings. The measured
reference setup used KDE at 125%, integer 2× backing and Java device transform 2×.
Other desktop scales and mixed-scale monitor moves are not validated.

Keep RuneLite's custom window chrome enabled for its title bar and controls.
“Keep game size” expands the outer window when the sidebar opens, provided the
window is not maximized. The wrapper remembers normal window dimensions.

## Limits

This is a workaround with a small **patched private Xwayland**, not a RuneLite
plugin or a native Wayland backend. There is still an upstream patch to maintain;
packaging makes its version, build and removal explicit.

- One RuneLite client per HiDPI environment. Native clients can run separately.
- Desktop position is not restored; KDE may reposition windows near screen edges.
- Closing RuneLite through its own close button is normal. Closing the outer host
  through KDE/Alt+F4 can bypass RuneLite's exit confirmation.
- Dialogs and menus are confined to the host surface. Clipboard, tray behaviour,
  external helper windows and mixed-scale monitors need more testing.
- Larger backing buffers cost GPU work. Gameplay performance and input latency
  have not been benchmarked. The user has played successfully with this route.
- The resize patch preserves backing pixels in regression tests; remaining
  in-game flicker needs visual confirmation.

## Update or remove

```bash
git pull --ff-only
makepkg -fsi
```

To remove our shortcuts and package, preserving account/profile data:

```bash
runelite-hidpi --undo
sudo pacman -R runelite-hidpi
```

Configuration lives in `~/.config/runelite-hidpi/`; saved sizes and private logs
in `~/.local/state/runelite-hidpi/`. **Do not share `launcher-private.log`**: it can
contain account information. No account data is part of this repository.

## Source

`PKGBUILD` pins Xwayland 24.1.14 and its SHA-256; pacman manages runtime/build
dependencies. Project source and assets live in `src/`: Bash launcher, minimal
C window manager, Xwayland patch and icon. The icon comes from the official
RuneLite AppImage; its required licence notice and Xwayland's upstream licence
are included in the installed package.

Historical experiments, tests and handoff notes are local-only under `.local/`;
they are not needed to build or install.
