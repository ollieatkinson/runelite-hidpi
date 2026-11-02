// The launcher prepends numeric settings before loading this script in KWin.
const trackedWindows = new Set();

function trackPosition(window) {
    if (trackedWindows.has(window))
        return;
    trackedWindows.add(window);

    // KWin exposes live geometry objects; copy the numbers before the next resize.
    function snapshot() {
        const frame = window.frameGeometry;
        return {x: frame.x, y: frame.y, width: frame.width, height: frame.height};
    }
    let previous = snapshot();
    let wasManaged = window.fullScreen || window.maximizeMode || window.tile;
    let correcting = false;
    window.frameGeometryChanged.connect(() => {
        if (correcting)
            return;
        const frame = snapshot();
        const managed = window.fullScreen || window.maximizeMode || window.tile;
        // Sidebar layout changes only the width. KWin otherwise keeps the centre
        // fixed during client-initiated resizing, moving the game underneath it.
        if (!window.move && !window.resize && !managed && !wasManaged &&
            Math.abs(frame.width - previous.width) > 0.5 &&
            Math.abs(frame.height - previous.height) < 0.5 &&
            (frame.x !== previous.x || frame.y !== previous.y)) {
            correcting = true;
            window.frameGeometry = {
                x: previous.x, y: previous.y, width: frame.width, height: frame.height,
            };
            correcting = false;
        }
        previous = snapshot();
        wasManaged = managed;
    });
}

function apply(window) {
    if (window.pid !== settings.pid)
        return;

    if (settings.track)
        trackPosition(window);

    if (settings.visible) {
        window.skipTaskbar = false;
        window.skipSwitcher = false;
        window.minimized = false;
        workspace.activeWindow = window;

        if (settings.center) {
            const area = workspace.clientArea(KWin.PlacementArea, window);
            const frame = window.frameGeometry;
            window.frameGeometry = {
                x: area.x + (area.width - frame.width) / 2,
                y: area.y + (area.height - frame.height) / 2,
                width: frame.width,
                height: frame.height,
            };
        }
        window.opacity = 1;
    } else {
        window.opacity = 0;
        window.minimized = true;
        window.skipTaskbar = true;
        window.skipSwitcher = true;
    }
}

for (const window of workspace.windowList())
    apply(window);
workspace.windowAdded.connect(apply);
