#!/usr/bin/env bash
# Stock Jagex/RuneLite in a private integer-backing Xwayland. No profile edits.
set -euo pipefail
umask 077
bindir=$(dirname -- "$(realpath -- "$0")")
libdir=${RUNELITE_HIDPI_LIBDIR:-$bindir/../lib/runelite-hidpi}
config=${XDG_CONFIG_HOME:-$HOME/.config}/runelite-hidpi
state=${XDG_STATE_HOME:-$HOME/.local/state}/runelite-hidpi
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
fail() {
    printf '%s\n' "$*" >&2
    if [[ ! -t 2 ]] && command -v kdialog >/dev/null; then kdialog --error "$*" || :; fi
    exit 1
}
# Desktop Exec has its own escaping rules, independent of shell quoting.
desktop_quote() {
    local value=$1
    value=${value//\\/\\\\\\\\}; value=${value//\"/\\\"}
    value=${value//\$/\\\$}; value=${value//\`/\\\`}; value=${value//%/%%}
    printf '"%s"' "$value"
}
case ${1:-} in
    --setup)
        [[ $# == 2 && -x $2 && -f $2 ]] || fail 'Usage: runelite-hidpi --setup /absolute/path/to/Jagex.AppImage'
        app=$(realpath -- "$2")
        [[ $app != *$'\n'* ]] || fail 'Launcher paths containing newlines are unsupported.'
        mkdir -p "$config" "$apps"
        printf '%s\n' "$app" > "$config/launcher"
        executable=$(realpath -- "$0")
        {
            printf '[Desktop Entry]\nType=Application\nName=Jagex Launcher (RuneLite HiDPI)\nExec='
            desktop_quote "$executable"
            printf ' %%U\nIcon=runelite-hidpi\nCategories=Game;\nTerminal=false\n'
        } > "$apps/jagex-launcher-runelite-hidpi.desktop"
        chmod 644 "$apps/jagex-launcher-runelite-hidpi.desktop"
        # Replace the former user-local identity entry so it cannot shadow the package.
        if [[ -f $apps/net.runelite.RuneLite.HiDPI.desktop ]]; then
            cp "$libdir/net.runelite.RuneLite.HiDPI.desktop" "$apps/net.runelite.RuneLite.HiDPI.desktop"
        fi
        printf 'Configured %s\nUse Jagex Launcher (RuneLite HiDPI).\n' "$app"
        exit 0;;
    --undo)
        for file in jagex-launcher-runelite-hidpi.desktop jagex-launcher-native.desktop net.runelite.RuneLite.HiDPI.desktop; do
            rm -f -- "$apps/$file"
        done
        printf 'Removed our user shortcuts. Profiles, configuration and saved sizes retained.\n'
        exit 0;;
    --help)
        printf 'runelite-hidpi --setup /path/to/Jagex.AppImage\nrunelite-hidpi [launcher URLs...]\nrunelite-hidpi --undo\n'
        exit 0;;
esac
[[ -n ${WAYLAND_DISPLAY:-} && ${XDG_CURRENT_DESKTOP:-} == *KDE* ]] || fail 'This experimental launcher requires KDE on Wayland.'
[[ -f $config/launcher ]] || fail 'First run: runelite-hidpi --setup /path/to/Jagex.AppImage'
IFS= read -r app < "$config/launcher"
for file in "$app" "$libdir/Xwayland" "$libdir/nested-wm"; do
    [[ -x $file ]] || fail "Required executable unavailable: $file"
done
mkdir -p "$state"; chmod 700 "$state"
exec 9>"$state/instance.lock"
# KDE stores the logical cursor size; the private X11 display renders at 2x.
cursor_theme=${XCURSOR_THEME:-$(kreadconfig6 --file kcminputrc --group Mouse --key cursorTheme --default default)}
cursor_size=${XCURSOR_SIZE:-$(kreadconfig6 --file kcminputrc --group Mouse --key cursorSize --default 24)}
[[ $cursor_size =~ ^[0-9]{1,3}$ ]] && ((10#$cursor_size > 0)) || cursor_size=24
client_environment() {
    export DISPLAY=$1 _JAVA_AWT_WM_NONREPARENTING=1
    export RUNELITE_VMARGS="${RUNELITE_VMARGS:-} -Dsun.java2d.uiScale=2"
    # XCURSOR_SIZE overrides X resources and cannot suit both the native Wayland
    # launcher and its X11 game. Let each display supply its own cursor size.
    unset XCURSOR_SIZE
}
publish_launcher_pid() {
    printf '%s\n' "$1" > "$state/launcher.pid.tmp"
    mv -- "$state/launcher.pid.tmp" "$state/launcher.pid"
}
if ! flock -n 9; then
    jq -e '.trial == false' "$state/active.json" >/dev/null 2>&1 || fail 'HiDPI launcher is starting or uses an isolated trial profile. Close it before switching routes.'
    pid=$(jq -er '.server_pid | select(type=="number")' "$state/active.json" 2>/dev/null) || fail 'HiDPI launcher is still starting. Try again shortly.'
    display=$(jq -er '.display | select(test("^:[0-9]+$"))' "$state/active.json")
    kill -0 "$pid" 2>/dev/null || fail 'HiDPI launcher is still stopping. Try again shortly.'
    client_environment "$display"
    publish_launcher_pid "$$"
    exec "$app" --ozone-platform=wayland "$@" 9>&- >/dev/null 2>&1
fi
# Electron must not forward Play into a launcher on another display.
for commfile in /proc/[0-9]*/comm; do
    [[ -O $commfile ]] || continue
    IFS= read -r comm < "$commfile" 2>/dev/null || continue
    if [[ ${comm,,} == *jagex* ]]; then
        fail 'Quit the existing Jagex launcher first. Existing RuneLite clients can stay running.'
    fi
done
display=''
for number in {12..39}; do
    if [[ ! -e /tmp/.X11-unix/X$number && ! -e /tmp/.X$number-lock ]]; then display=:$number; break; fi
done
[[ -n $display ]] || fail 'No unused private X11 display.'
geometry=$(jq -er 'select(.width>=320 and .width<=16383 and .height>=200 and .height<=16383) | "\(.width|floor)x\(.height|floor)"' "$state/window.json" 2>/dev/null) || geometry=1557x762
server_pid='' wm_pid='' watcher_pid='' launcher_pid=''
publish_client() {
    jq -n --argjson present "$1" --argjson pid "$2" '{present:$present,pid:$pid}' > "$state/client.json.tmp"
    mv -- "$state/client.json.tmp" "$state/client.json"
}
host_visibility() {
    local visible=$1 center=${2:-0} persistent=${3:-0}
    local kind=window
    if [[ $persistent == 1 ]]; then kind=host; fi
    local script="$state/$kind-$server_pid.js" name="runelite-hidpi-$kind-$server_pid" id
    {
        printf 'const settings = {pid:%s,visible:%s,center:%s,track:%s};\n' \
            "$server_pid" "$visible" "$center" "$persistent"
        cat "$libdir/host-window.js"
    } > "$script"
    id=$(timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$script" "$name" 2>/dev/null) || return 0
    if [[ $id =~ ^[0-9]+$ ]]; then
        # KWin can reuse an occupied D-Bus script ID after another script unloads.
        # Starting through Scripting reaches the loaded object; running scripts stay running.
        timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1 || :
        if [[ $persistent == 0 ]]; then sleep .25; fi
    fi
    [[ $persistent == 0 ]] || return 0
    timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$name" >/dev/null 2>&1 || :
    rm -f -- "$script"
}
cleanup() {
    trap - EXIT INT TERM
    for pid in "$watcher_pid" "$launcher_pid" "$wm_pid" "$server_pid"; do
        [[ -n $pid ]] || continue
        kill "$pid" 2>/dev/null || :
    done
    if [[ -n $server_pid ]]; then
        for kind in host window; do
            timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "runelite-hidpi-$kind-$server_pid" >/dev/null 2>&1 || :
            rm -f -- "$state/$kind-$server_pid.js"
        done
    fi
    rm -f -- "$state/active.json" "$state/wm.pipe" "$state/server.pipe" "$state/client-present" "$state/client.json" "$state/client.json.tmp" "$state/launcher.pid" "$state/launcher.pid.tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for log in server.log wm.log launcher-private.log; do : > "$state/$log"; chmod 600 "$state/$log"; done
export RUNELITE_HIDPI_APP_ID=net.runelite.RuneLite.HiDPI
rm -f -- "$state/server.pipe"
mkfifo "$state/server.pipe"
exec 3<>"$state/server.pipe"
(
    IFS= read -r -u 3
    exec 3>&-
    if [[ ${RUNELITE_HIDPI_DEBUG:-0} == 1 ]]; then export WAYLAND_DEBUG=1; fi
    exec "$libdir/Xwayland" "$display" -geometry "$geometry" -hidpi -clipboard -nolisten tcp
) 9>&- >"$state/server.log" 2>&1 &
server_pid=$!
# Install the owned-window handler before the server can map its empty root.
# Keep it loaded to anchor later sidebar resizes, independent of visibility scripts.
host_visibility 0 0 1
printf '\n' >&3
exec 3>&-
rm -f -- "$state/server.pipe"
client_environment "$display"
ready=0
for ((attempt=0;attempt<60;attempt++)); do
    kill -0 "$server_pid" 2>/dev/null || fail "Private Xwayland exited; see $state/server.log"
    if xdpyinfo >/dev/null 2>&1; then ready=1; break; fi
    sleep .2
done
[[ $ready == 1 ]] || fail 'Private Xwayland did not become ready.'
host_visibility 0
rm -f -- "$state/wm.pipe"
mkfifo "$state/wm.pipe"
exec 4<>"$state/wm.pipe"
"$libdir/nested-wm" 9>&- 4>&- > "$state/wm.pipe" 2>&1 &
wm_pid=$!
# A fast client must not map before the WM has selected MapRequest events.
IFS= read -r -t 5 -u 4 line && [[ $line == READY ]] || fail 'Private window manager did not become ready.'
# Keep the WM connected before setting resources, or the X server's last-client
# reset will discard them. Java's interface scale is configured independently.
printf 'Xcursor.theme: %s\nXcursor.size: %s\n' "$cursor_theme" "$((10#$cursor_size * 2))" |
    xrdb -nocpp -merge
publish_client false 0
(
    maximized=0
    visible=0
    present=0
    handoff=0
    startup=1
    client_pid=0
    while true; do
        # The splash closes just before the main window maps. Keep the host
        # visible through that handoff instead of minimizing and restoring it.
        if [[ $present == 0 && $visible == 1 ]]; then
            if ! IFS= read -r -t .4 line; then
                host_visibility 0
                visible=0
                startup=1
                publish_client false "$client_pid"
                continue
            fi
        else
            IFS= read -r line || break
        fi
        printf '%s\n' "$line" >> "$state/wm.log"
        case $line in
            'STATE 1') maximized=1;;
            'STATE 0') maximized=0;;
            'ROOT '*)
                if [[ $maximized == 0 && $line =~ ^ROOT\ ([0-9]+)x([0-9]+)$ ]]; then
                    jq -n --argjson width "$((BASH_REMATCH[1]/2))" --argjson height "$((BASH_REMATCH[2]/2))" '{width:$width,height:$height}' > "$state/window.json.tmp"
                    mv -- "$state/window.json.tmp" "$state/window.json"
                fi;;
            'CLIENT mapped '*)
                present=1
                if [[ $line =~ ^CLIENT\ mapped\ ([0-9]+)$ && ${BASH_REMATCH[1]} != 0 ]]; then
                    client_pid=${BASH_REMATCH[1]}
                fi
                publish_client true "$client_pid";;
            'CLIENT '*)
                if [[ $line == *shown* ]]; then present=1; else present=0; handoff=1; fi
                if [[ $present == 1 ]]; then
                    if [[ $line =~ ^CLIENT\ shown\ ([0-9]+)$ && ${BASH_REMATCH[1]} != 0 ]]; then
                        client_pid=${BASH_REMATCH[1]}
                    fi
                    publish_client true "$client_pid"
                fi
                if [[ $present == 1 && $visible == 0 ]]; then
                    host_visibility 1 1
                    visible=1
                elif [[ $present == 1 && $handoff == 1 ]]; then
                    host_visibility 1 1
                    startup=0
                elif [[ $present == 0 && $startup == 0 ]]; then
                    host_visibility 0
                    visible=0
                    startup=1
                    publish_client false "$client_pid"
                fi
                if [[ $present == 1 ]]; then handoff=0; fi;;
        esac
    done < "$state/wm.pipe"
) 9>&- 4>&- &
watcher_pid=$!
exec 4>&-
setsid "$app" --ozone-platform=wayland "$@" 9>&- > "$state/launcher-private.log" 2>&1 &
launcher_pid=$!
publish_launcher_pid "$launcher_pid"
jq -n --argjson server_pid "$server_pid" --arg display "$display" '{server_pid:$server_pid,display:$display,trial:false}' > "$state/active.json"
printf 'Jagex launcher opened. Select RuneLite and click Play.\nNormal ~/.runelite; 2x backing, KDE desktop output scale.\n'
while kill -0 "$server_pid" 2>/dev/null; do
    kill -0 "$wm_pid" 2>/dev/null || fail 'Private window manager exited.'
    read -r client_pid present < <(jq -r '[.pid,.present] | @tsv' "$state/client.json")
    IFS= read -r current_launcher_pid < "$state/launcher.pid"
    if ! kill -0 "$current_launcher_pid" 2>/dev/null && [[ $present != true ]]; then
        # The game can outlive its launcher, including periods with no mapped window.
        if [[ $client_pid == 0 ]] || ! kill -0 "$client_pid" 2>/dev/null; then break; fi
    fi
    sleep .5
done
