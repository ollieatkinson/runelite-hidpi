# RuneLite HiDPI

A workaround for RuneLite being too small on KDE Wayland with fractional scaling.
Built and used on CachyOS at 125% desktop scale.

It starts a private, patched Xwayland server with a 2× backing buffer. RuneLite
renders at 2× and KDE downsamples the whole window to your desktop scale. This
includes the game, sidebar and RuneLite title bar, without using Stretched Mode.

It uses your installed Linux Jagex Launcher and RuneLite AppImages, with your
normal account and `~/.runelite` folder. It doesn't replace either AppImage or
the system Xwayland.

## Install

For Arch/CachyOS with KDE Wayland. Install RuneLite through Jagex Launcher first.
This repo is private, so you'll need access to clone it.

```sh
sudo pacman -S --needed base-devel git
git clone https://github.com/ollieatkinson/runelite-hidpi.git
cd runelite-hidpi
makepkg -si
runelite-hidpi --setup "$HOME/Applications/jagex-launcher-beta-linux-x86_64.AppImage"
```

Use the actual path to your Jagex AppImage in the last command.

Quit Jagex Launcher, then open **Jagex Launcher (RuneLite HiDPI)** from your
application menu and click Play. There's also a **Jagex Launcher (native)**
shortcut for launching normally. Existing RuneLite clients can stay open.

In KDE's Display Configuration, use **Apply scaling themselves** for X11 apps.
The wrapper doesn't change this setting. Keep RuneLite's custom window chrome
enabled if you want its title bar and buttons.

## Rough edges

- One RuneLite client per HiDPI launch.
- Normal window size is remembered; position isn't.
- “Keep game size” works with the sidebar when the window isn't maximized.
  KDE may move the window if it grows past the screen edge.
- Use RuneLite's close button. Closing the outer window through KDE can bypass
  RuneLite's exit confirmation.
- Dialogs and menus stay inside the outer window. Clipboard, tray behaviour and
  moving between monitors with different scales haven't been fully checked.
- Resizing may still briefly flicker. Gameplay performance hasn't been benchmarked.

This still needs a small Xwayland patch. It isn't a RuneLite plugin or a native
Wayland backend. `PKGBUILD` pins the Xwayland version and pacman manages the
dependencies. The launcher is Bash; the window manager is C. Source is in `src/`.

## Update

```sh
git pull --ff-only
makepkg -fsi
```

Close the HiDPI client and launcher before relaunching to use the update.

## Remove

```sh
runelite-hidpi --undo
sudo pacman -R runelite-hidpi
```

Your account and RuneLite settings are left alone. The wrapper keeps its config
in `~/.config/runelite-hidpi/` and saved sizes/logs in
`~/.local/state/runelite-hidpi/`. Don't share `launcher-private.log`; it may
contain account information.
