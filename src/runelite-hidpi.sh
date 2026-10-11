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
        for mode in hidpi native; do
            if [[ $mode == hidpi ]]; then
                name='Jagex Launcher (RuneLite HiDPI)'; target=$executable
                filename=jagex-launcher-runelite-hidpi.desktop
            else
                name='Jagex Launcher (native)'; target=$app
                filename=jagex-launcher-native.desktop
            fi
            {
                printf '[Desktop Entry]\nType=Application\nName=%s\nExec=' "$name"
                desktop_quote "$target"
                printf ' %%U\nIcon=runelite-hidpi\nCategories=Game;\nTerminal=false\n'
            } > "$apps/$filename"
            chmod 644 "$apps/$filename"
        done
        # Replace the former user-local identity entry so it cannot shadow the package.
        if [[ -f $apps/net.runelite.RuneLite.HiDPI.desktop ]]; then
            cp "$libdir/net.runelite.RuneLite.HiDPI.desktop" "$apps/net.runelite.RuneLite.HiDPI.desktop"
        fi
        printf 'Configured %s\nUse Jagex Launcher (RuneLite HiDPI) or Jagex Launcher (native).\n' "$app"
        exit 0;;
    --undo)
        for file in jagex-launcher-runelite-hidpi.desktop jagex-launcher-native.desktop net.runelite.RuneLite.HiDPI.desktop; do
            rm -f -- "$apps/$file"
        done
        # Undo the older installer's hiding of the launcher-owned entry.
        if [[ -f $apps/jagex-launcher.desktop ]]; then
            sed -i '/^NoDisplay=true$/d' "$apps/jagex-launcher.desktop"
        fi
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
client_environment() {
    export DISPLAY=$1 _JAVA_AWT_WM_NONREPARENTING=1
    export RUNELITE_VMARGS="${RUNELITE_VMARGS:-} -Dsun.java2d.uiScale=2"
}
if ! flock -n 9; then
    jq -e '.trial == false' "$state/active.json" >/dev/null 2>&1 || fail 'HiDPI launcher is starting or uses an isolated trial profile. Close it before switching routes.'
    pid=$(jq -er '.server_pid | select(type=="number")' "$state/active.json" 2>/dev/null) || fail 'HiDPI launcher is still starting. Try again shortly.'
    display=$(jq -er '.display | select(test("^:[0-9]+$"))' "$state/active.json")
    kill -0 "$pid" 2>/dev/null || fail 'HiDPI launcher is still stopping. Try again shortly.'
    client_environment "$display"
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
host_visibility() {
    local visible=$1 script="$state/window-$server_pid.js" name="runelite-hidpi-window-$server_pid" operations id
    if [[ $visible == 1 ]]; then
        operations='w.skipTaskbar=false;w.skipSwitcher=false;w.minimized=false;workspace.activeWindow=w;'
    else
        operations='w.minimized=true;w.skipTaskbar=true;w.skipSwitcher=true;'
    fi
    printf 'function apply(w){if(w.pid===%s){%s}};for(const w of workspace.windowList())apply(w);workspace.windowAdded.connect(apply);\n' "$server_pid" "$operations" > "$script"
    id=$(timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$script" "$name" 2>/dev/null) || return 0
    if [[ $id =~ ^[0-9]+$ ]]; then
        timeout 5 qdbus6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run >/dev/null 2>&1 || :
        sleep .25
    fi
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
        timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "runelite-hidpi-window-$server_pid" >/dev/null 2>&1 || :
        rm -f -- "$state/window-$server_pid.js"
    fi
    rm -f -- "$state/active.json" "$state/wm.pipe" "$state/client-present"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for log in server.log wm.log launcher-private.log; do : > "$state/$log"; chmod 600 "$state/$log"; done
export RUNELITE_HIDPI_APP_ID=net.runelite.RuneLite.HiDPI
if [[ ${RUNELITE_HIDPI_DEBUG:-0} == 1 ]]; then
    WAYLAND_DEBUG=1 "$libdir/Xwayland" "$display" -geometry "$geometry" -hidpi -nolisten tcp 9>&- >"$state/server.log" 2>&1 &
else
    "$libdir/Xwayland" "$display" -geometry "$geometry" -hidpi -nolisten tcp 9>&- >"$state/server.log" 2>&1 &
fi
server_pid=$!
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
printf '0\n' > "$state/client-present"
(
    maximized=0
    while IFS= read -r line; do
        printf '%s\n' "$line" >> "$state/wm.log"
        case $line in
            'STATE 1') maximized=1;;
            'STATE 0') maximized=0;;
            'ROOT '*)
                if [[ $maximized == 0 && $line =~ ^ROOT\ ([0-9]+)x([0-9]+)$ ]]; then
                    jq -n --argjson width "$((BASH_REMATCH[1]/2))" --argjson height "$((BASH_REMATCH[2]/2))" '{width:$width,height:$height}' > "$state/window.json.tmp"
                    mv -- "$state/window.json.tmp" "$state/window.json"
                fi;;
            'CLIENT '*)
                if [[ $line == *shown* ]]; then present=1; else present=0; fi
                printf '%s\n' "$present" > "$state/client-present"
                host_visibility "$present";;
        esac
    done < "$state/wm.pipe"
) 9>&- &
watcher_pid=$!
"$libdir/nested-wm" 9>&- > "$state/wm.pipe" 2>&1 &
wm_pid=$!
setsid "$app" --ozone-platform=wayland "$@" 9>&- > "$state/launcher-private.log" 2>&1 &
launcher_pid=$!
jq -n --argjson server_pid "$server_pid" --arg display "$display" '{server_pid:$server_pid,display:$display,trial:false}' > "$state/active.json"
printf 'Jagex launcher opened. Select RuneLite and click Play.\nNormal ~/.runelite; 2x backing, KDE desktop output scale.\n'
while kill -0 "$server_pid" 2>/dev/null; do
    kill -0 "$wm_pid" 2>/dev/null || fail 'Private window manager exited.'
    present=$(cat "$state/client-present")
    if ! kill -0 "$launcher_pid" 2>/dev/null && [[ $present != 1 ]]; then break; fi
    sleep .5
done
