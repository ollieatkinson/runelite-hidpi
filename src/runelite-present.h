#ifndef RUNELITE_PRESENT_H
#define RUNELITE_PRESENT_H

#include <X11/X.h>
#include <X11/Xdefs.h>

struct xwl_window;
struct wl_callback;

struct runelite_present {
    XID client, notified;
    struct wl_callback *sync;
    Bool painted;
};

void runelite_present_client(struct xwl_window *window, XID client);
void runelite_present_unmap(struct xwl_window *window, XID client);
Bool runelite_present_waiting(struct xwl_window *window);
void runelite_present_commit(struct xwl_window *window);
void runelite_present_fini(struct xwl_window *window);

#endif
