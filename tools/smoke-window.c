/* Synthetic client for an isolated KWin + patched Xwayland + nested-wm test.
 * cc tools/smoke-window.c -o /tmp/smoke-window -lX11 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *d;
static Window root, window;

static void require(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}

static void wait_size(int width, int height)
{
    for (int i = 0; i < 200; i++) {
        XWindowAttributes r, w;
        XGetWindowAttributes(d, root, &r); XGetWindowAttributes(d, window, &w);
        if (r.width == width && r.height == height && w.width == width && w.height == height)
            return;
        usleep(10000);
    }
    require(0, "Client/host size failed to converge");
}

static void wait_maximized(int state)
{
    Atom property = XInternAtom(d, "_RUNELITE_HOST_STATE", False);
    for (int i = 0; i < 200; i++) {
        Atom type; int format; unsigned long count, remaining; unsigned char *data = NULL;
        XGetWindowProperty(d, root, property, 0, 1, False, XA_CARDINAL,
                           &type, &format, &count, &remaining, &data);
        int matches = data && format == 32 && count == 1 &&
                      ((*(unsigned long *)data & 1) != 0) == state;
        if (data) XFree(data);
        if (matches) return;
        usleep(10000);
    }
    require(0, "Host maximize state failed to converge");
}

static void maximize(int state)
{
    XEvent event = {0};
    event.xclient.type = ClientMessage; event.xclient.window = window;
    event.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    event.xclient.format = 32; event.xclient.data.l[0] = state;
    event.xclient.data.l[1] = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    event.xclient.data.l[2] = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(d); wait_maximized(state);
}

int main(int argc, char **argv)
{
    d = XOpenDisplay(NULL); require(d != NULL, "Cannot open test display");
    root = DefaultRootWindow(d);
    window = XCreateSimpleWindow(d, root, 0, 0, 600, 400, 0, 0, 0x303030);
    XStoreName(d, window, "RuneLite filter smoke test");
    unsigned long motif[] = {2, 0, 0, 0, 0};
    Atom hints = XInternAtom(d, "_MOTIF_WM_HINTS", False);
    XChangeProperty(d, window, hints, hints, 32, PropModeReplace, (unsigned char *)motif, 5);
    XSizeHints sizes = {.flags = PMinSize, .min_width = 320, .min_height = 200};
    XSetWMNormalHints(d, window, &sizes);
    XMapWindow(d, window); XFlush(d);
    /* Let the initial host configure and decoration negotiation settle. */
    usleep(400000);
    XResizeWindow(d, window, 600, 400); XFlush(d); wait_size(600, 400);
    XResizeWindow(d, window, 800, 400); XFlush(d); wait_size(800, 400);
    Window dialog = XCreateSimpleWindow(d, root, 150, 100, 120, 80, 0, 0, 0x606060);
    XSetTransientForHint(d, dialog, window); XMapWindow(d, dialog); XSync(d, False);
    usleep(100000); XDestroyWindow(d, dialog); XFlush(d); wait_size(800, 400);
    maximize(1); usleep(100000); maximize(0); wait_size(800, 400);
    GC gc = XCreateGC(d, window, 0, NULL);
    int frames = argc > 1 ? atoi(argv[1]) : 120;
    require(frames >= 120 && frames <= 1000, "Invalid repaint duration");
    for (int i = 0; i < frames; i++) {
        XSetForeground(d, gc, i & 1 ? 0xffffff : 0x000000);
        XFillRectangle(d, window, gc, 0, 0, 800, 400);
        XFlush(d); usleep(10000);
    }
    XFreeGC(d, gc); XDestroyWindow(d, window); XSync(d, False); XCloseDisplay(d);
    puts("Resize, sidebar-sized expansion, dialog, maximize/restore, repeated repaint: PASS");
    return 0;
}
