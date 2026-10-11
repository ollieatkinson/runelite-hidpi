#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
# Load the real helper functions without starting the graphical session.
source <(sed -n '/^resolve_launcher()/,/^case /p' "$root/src/runelite-hidpi.sh" | sed '$d')
apps=$tmp/apps
config=$tmp/config
mkdir -p "$apps" "$config"
old="$tmp/old.AppImage"
new="$tmp/new launcher.AppImage"
printf '#!/bin/sh\nexit 0\n' > "$old"
cp "$old" "$new"
chmod +x "$old" "$new"
printf '%s\n' "$old" > "$config/launcher"
printf '[Desktop Entry]\nExec="%s" %%U\nTryExec=%s\n' "$new" "$new" > "$apps/jagex-launcher.desktop"
resolve_launcher
[[ $app == "$new" ]] # Updated normal shortcut takes precedence over saved path.
rm "$old"
resolve_launcher
[[ $app == "$new" ]] # Relocation needs no setup rerun.
printf '[Desktop Entry]\nExec="%s" %%U\n' "$new" > "$apps/jagex-launcher.desktop"
resolve_launcher
[[ $app == "$new" ]] # Quoted Exec works without TryExec.
printf '[Desktop Entry]\nExec=sh -c touch %s\n' "$tmp/unsafe" > "$apps/jagex-launcher.desktop"
printf '%s\n' "$new" > "$config/launcher"
resolve_launcher
[[ $app == "$new" && ! -e $tmp/unsafe ]] # Unsupported commands are never evaluated.
printf 'Launcher discovery checks passed.\n'
