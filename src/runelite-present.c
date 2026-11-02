/* Retain the last client frame during unmap/map handoffs. The nested WM waits
 * for paint, and only reveals the surface once Wayland has consumed its commit. */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/Xatom.h>
#include <string.h>
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "damage.h"
#include "runelite-present.h"

void runelite_present_fini(struct xwl_window *w)
{
    if (w->runelite_present.sync) {
        wl_callback_destroy(w->runelite_present.sync);
        w->runelite_present.sync = NULL;
    }
}

void runelite_present_client(struct xwl_window *w, XID client)
{
    runelite_present_fini(w);
    /* Hidden surfaces can be throttled indefinitely. An old blank frame's
     * callback must not prevent submission of the new client's first paint. */
    if (w->frame_callback) {
        wl_callback_destroy(w->frame_callback);
        w->frame_callback = NULL;
    }
    w->runelite_present.client = client;
    w->runelite_present.notified = None;
    w->runelite_present.painted = TRUE;
    DamageDamageRegion(&w->surface_window->drawable, &w->surface_window->winSize);
}

void runelite_present_unmap(struct xwl_window *w, XID client)
{
    if (!w || w->runelite_present.client != client)
        return;
    runelite_present_fini(w);
    /* A remapped window can reuse its XID, but still needs a fresh paint. */
    w->runelite_present.painted = FALSE;
    w->runelite_present.notified = None;
}

Bool runelite_present_waiting(struct xwl_window *w)
{
    WindowPtr client;
    if (w->xwl_screen->rootless || !w->runelite_present.client)
        return FALSE;
    return !w->runelite_present.painted ||
           dixLookupWindow(&client, w->runelite_present.client, serverClient, DixReadAccess) != Success ||
           !client->mapped;
}

static void frame_ready(void *data, struct wl_callback *callback, uint32_t serial)
{
    struct xwl_window *w = data;
    wl_callback_destroy(callback);
    w->runelite_present.sync = NULL;
    if (runelite_present_waiting(w))
        return;
    const char *name = "_RUNELITE_FRAME_READY";
    Atom atom = MakeAtom(name, strlen(name), TRUE);
    CARD32 client = w->runelite_present.client;
    w->runelite_present.notified = client;
    dixChangeWindowProperty(serverClient, w->xwl_screen->screen->root, atom,
                            XA_WINDOW, 32, PropModeReplace, 1, &client, TRUE);
}

static const struct wl_callback_listener ready_listener = { frame_ready };

void runelite_present_commit(struct xwl_window *w)
{
    struct runelite_present *present = &w->runelite_present;
    /* post_damage creates a frame callback only after attaching a buffer.
     * A display sync, unlike a frame callback, also completes while hidden. */
    if (w->frame_callback && present->client && present->painted &&
        present->notified != present->client && !present->sync) {
        present->sync = wl_display_sync(w->xwl_screen->display);
        wl_callback_add_listener(present->sync, &ready_listener, w);
    }
}
