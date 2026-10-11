#!/usr/bin/env bash
# Account-free integration test. Does not launch Jagex or inspect profiles.
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: check-resize.sh Xwayland nested-wm resize-test' >&2; exit 1; }
[[ -n ${WAYLAND_DISPLAY:-} ]] || { echo 'Requires a Wayland session.' >&2; exit 1; }
umask 077
tmp=$(mktemp -d)
server_pid='' wm_pid=''
cleanup() {
    for pid in "$wm_pid" "$server_pid"; do [[ -z $pid ]] || kill "$pid" 2>/dev/null || :; done
    rm -rf -- "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for number in {20..39}; do
    if [[ ! -e /tmp/.X11-unix/X$number && ! -e /tmp/.X$number-lock ]]; then display=:$number; break; fi
done
: "${display:?No unused test display}"
# Explicit allowlist: inherited Jagex authentication variables cannot reach probes.
testenv=(env -i "PATH=$PATH" "LANG=${LANG:-C.UTF-8}" "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR" "WAYLAND_DISPLAY=$WAYLAND_DISPLAY" "DISPLAY=$display")
"${testenv[@]}" "$1" "$display" -geometry 1000x600 -hidpi -nolisten tcp > "$tmp/server.log" 2>&1 &
server_pid=$!
ready=0
for ((attempt=0;attempt<50;attempt++)); do
    kill -0 "$server_pid" 2>/dev/null || { cat "$tmp/server.log" >&2; exit 1; }
    if "${testenv[@]}" xdpyinfo >/dev/null 2>&1; then ready=1; break; fi
    sleep .1
done
[[ $ready == 1 ]] || { echo 'Test server did not become ready.' >&2; exit 1; }
"${testenv[@]}" "$2" > "$tmp/wm.log" 2>&1 &
wm_pid=$!
sleep .2
"${testenv[@]}" timeout 15 "$3"
echo 'PASS: resize, retained backing pixels, sidebar click vs drag, maximize/restore'
