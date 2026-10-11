/* Single-client X11 window manager for the private rootful Xwayland host. */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum HostAction {
    HOST_MOVE = 1,
    HOST_RESIZE = 2,
    HOST_MAXIMIZE = 3,
    HOST_RESTORE = 4,
    HOST_MINIMIZE = 5,
    HOST_CLIENT_CHROME = 6,
    HOST_VISIBILITY = 7,
    HOST_SET_SIZE = 8,
    HOST_SET_MIN_SIZE = 9,
    HOST_SET_CLIENT = 10,
};

enum AtomIndex {
    HOST_ACTION,
    HOST_TITLE,
    HOST_STATE,
    UTF8_STRING,
    WM_STATE,
    WM_CHANGE_STATE,
    MOTIF_WM_HINTS,
    NET_WM_STATE,
    NET_WM_STATE_MAXIMIZED_HORZ,
    NET_WM_STATE_MAXIMIZED_VERT,
    NET_WM_MOVERESIZE,
    NET_WM_NAME,
    NET_FRAME_EXTENTS,
    NET_ACTIVE_WINDOW,
    NET_SUPPORTED,
    NET_SUPPORTING_WM_CHECK,
    ATOM_COUNT,
};

static char *atom_names[ATOM_COUNT] = {
    [HOST_ACTION] = "_RUNELITE_HOST_ACTION",
    [HOST_TITLE] = "_RUNELITE_HOST_TITLE",
    [HOST_STATE] = "_RUNELITE_HOST_STATE",
    [UTF8_STRING] = "UTF8_STRING",
    [WM_STATE] = "WM_STATE",
    [WM_CHANGE_STATE] = "WM_CHANGE_STATE",
    [MOTIF_WM_HINTS] = "_MOTIF_WM_HINTS",
    [NET_WM_STATE] = "_NET_WM_STATE",
    [NET_WM_STATE_MAXIMIZED_HORZ] = "_NET_WM_STATE_MAXIMIZED_HORZ",
    [NET_WM_STATE_MAXIMIZED_VERT] = "_NET_WM_STATE_MAXIMIZED_VERT",
    [NET_WM_MOVERESIZE] = "_NET_WM_MOVERESIZE",
    [NET_WM_NAME] = "_NET_WM_NAME",
    [NET_FRAME_EXTENTS] = "_NET_FRAME_EXTENTS",
    [NET_ACTIVE_WINDOW] = "_NET_ACTIVE_WINDOW",
    [NET_SUPPORTED] = "_NET_SUPPORTED",
    [NET_SUPPORTING_WM_CHECK] = "_NET_SUPPORTING_WM_CHECK",
};

static Display *display;
static Window root_window, client_window;
static Atom atoms[ATOM_COUNT];
static int root_width, root_height;
static bool maximized;

static void send_host_action(enum HostAction action, unsigned long a, unsigned long b)
{
    /* Xlib uses longs for 32-bit property elements, including on LP64. */
    unsigned long values[] = {action, a, b};
    XChangeProperty(display, root_window, atoms[HOST_ACTION], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)values, 3);
}

static void read_root_geometry(void)
{
    XWindowAttributes attributes;
    XGetWindowAttributes(display, root_window, &attributes);
    root_width = attributes.width;
    root_height = attributes.height;
}

static void resize_host(int width, int height)
{
    send_host_action(HOST_SET_SIZE, width, height);
    XSync(display, False);
    read_root_geometry();
}

static void fit_client_to_host(void)
{
    if (!client_window)
        return;

    XMoveResizeWindow(display, client_window, 0, 0, root_width, root_height);
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xconfigure.type = ConfigureNotify;
    event.xconfigure.display = display;
    event.xconfigure.event = client_window;
    event.xconfigure.window = client_window;
    event.xconfigure.width = root_width;
    event.xconfigure.height = root_height;
    event.xconfigure.send_event = True;
    event.xconfigure.x = 0;
    event.xconfigure.y = 0;
    event.xconfigure.border_width = 0;
    event.xconfigure.above = None;
    event.xconfigure.override_redirect = False;
    XSendEvent(display, client_window, False, StructureNotifyMask, &event);
}

static void set_wm_state(Window window, long state)
{
    unsigned long values[] = {state, None};
    XChangeProperty(display, window, atoms[WM_STATE], atoms[WM_STATE], 32,
                    PropModeReplace, (unsigned char *)values, 2);
}

static void publish_maximized_state(void)
{
    if (client_window) {
        Atom state[] = {atoms[NET_WM_STATE_MAXIMIZED_HORZ], atoms[NET_WM_STATE_MAXIMIZED_VERT]};
        XChangeProperty(display, client_window, atoms[NET_WM_STATE], XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)state, maximized ? 2 : 0);
    }
    printf("STATE %d\n", maximized);
}

static void sync_host_state(void)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;

    XGetWindowProperty(display, root_window, atoms[HOST_STATE], 0, 1, False,
                       XA_CARDINAL, &type, &format, &count, &remaining, &data);
    if (data && format == 32 && count == 1) {
        maximized = (*(unsigned long *)data & 1) != 0;
        publish_maximized_state();
    }
    if (data)
        XFree(data);
}

static void sync_title(void)
{
    char *name = NULL;
    XFetchName(display, client_window, &name);
    if (name) {
        XChangeProperty(display, root_window, atoms[HOST_TITLE], atoms[UTF8_STRING], 8,
                        PropModeReplace, (unsigned char *)name, strlen(name));
        XFree(name);
    }
}

static void sync_minimum_size(void)
{
    XSizeHints hints;
    long supplied;
    if (XGetWMNormalHints(display, client_window, &hints, &supplied) && (hints.flags & PMinSize))
        send_host_action(HOST_SET_MIN_SIZE, hints.min_width, hints.min_height);
}

static void sync_chrome(void)
{
    const unsigned long decorations_hint = 1UL << 1;
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;

    XGetWindowProperty(display, client_window, atoms[MOTIF_WM_HINTS], 0, 5, False,
                       AnyPropertyType, &type, &format, &count, &remaining, &data);
    if (data && format == 32 && count >= 3) {
        unsigned long *hints = (unsigned long *)data;
        if ((hints[0] & decorations_hint) && hints[2] == 0)
            send_host_action(HOST_CLIENT_CHROME, 0, 0);
    }
    if (data)
        XFree(data);
}

static void handle_map_request(const XMapRequestEvent *event)
{
    Window window = event->window;
    Window transient_for = None;
    XGetTransientForHint(display, window, &transient_for);
    set_wm_state(window, NormalState);

    if (!client_window && !transient_for) {
        client_window = window;
        send_host_action(HOST_SET_CLIENT, window, 0);
        maximized = false;
        unsigned long extents[] = {0, 0, 0, 0};
        XChangeProperty(display, window, atoms[NET_FRAME_EXTENTS], XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)extents, 4);
        XSelectInput(display, window, PropertyChangeMask);
        XGrabButton(display, Button1, AnyModifier, window, False,
                    ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
        sync_host_state();
        sync_title();
        sync_chrome();
        sync_minimum_size();

        /* A saved host size can include a sidebar that the client starts closed. */
        XWindowAttributes attributes;
        if (!maximized && XGetWindowAttributes(display, window, &attributes) &&
            (attributes.width != root_width || attributes.height != root_height))
            resize_host(attributes.width, attributes.height);

        fit_client_to_host();
        XChangeProperty(display, root_window, atoms[NET_ACTIVE_WINDOW], XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&window, 1);
    }
    XMapWindow(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    if (window == client_window) {
        XSync(display, False);
        send_host_action(HOST_VISIBILITY, 1, 0);
        XSync(display, False);
        puts("CLIENT shown");
    }
}

static bool left_button_pressed(void)
{
    Window root, child;
    int root_x, root_y, window_x, window_y;
    unsigned mask;
    return XQueryPointer(display, root_window, &root, &child,
                         &root_x, &root_y, &window_x, &window_y, &mask) && (mask & Button1Mask);
}

static void handle_button_press(const XButtonEvent *event)
{
    /* Hand border drags to the host: the nested screen ends at this border. */
    const int border = 8;
    XSizeHints hints;
    long supplied;
    unsigned edge = 0;
    bool fixed = XGetWMNormalHints(display, client_window, &hints, &supplied) &&
                 (hints.flags & PMinSize) && (hints.flags & PMaxSize) &&
                 hints.min_width == hints.max_width && hints.min_height == hints.max_height;

    if (event->window == client_window && event->button == Button1 && !maximized && !fixed) {
        if (event->x < border)
            edge |= 4;
        else if (event->x >= root_width - border)
            edge |= 8;
        if (event->y < border)
            edge |= 1;
        else if (event->y >= root_height - border)
            edge |= 2;
    }

    if (edge) {
        XUngrabPointer(display, event->time);
        send_host_action(HOST_RESIZE, edge, 0);
    } else {
        XAllowEvents(display, ReplayPointer, event->time);
    }
}

static void handle_configure_request(const XConfigureRequestEvent *event)
{
    if (event->window != client_window) {
        XWindowChanges changes = {
            .x = event->x, .y = event->y,
            .width = event->width, .height = event->height,
            .border_width = event->border_width,
            .sibling = event->above, .stack_mode = event->detail,
        };
        XConfigureWindow(display, event->window, event->value_mask, &changes);
        return;
    }

    int width = (event->value_mask & CWWidth) ? event->width : root_width;
    int height = (event->value_mask & CWHeight) ? event->height : root_height;
    bool resizing = width != root_width || height != root_height;
    if (resizing)
        resize_host(width, height);

    /* Sidebar resizing can include containment coordinates while its click is held. */
    if (!resizing && (event->value_mask & (CWX | CWY)) && (event->x || event->y) && left_button_pressed())
        send_host_action(HOST_MOVE, 0, 0);

    fit_client_to_host();
}

static void handle_client_message(const XClientMessageEvent *event)
{
    if (event->window != client_window)
        return;

    if (event->message_type == atoms[NET_ACTIVE_WINDOW]) {
        XRaiseWindow(display, client_window);
        XSetInputFocus(display, client_window, RevertToParent, CurrentTime);
    } else if (event->message_type == atoms[NET_WM_MOVERESIZE]) {
        /* EWMH resize directions, clockwise from top-left, to xdg_toplevel edges. */
        static const unsigned edges[] = {5, 1, 9, 8, 10, 2, 6, 4};
        long direction = event->data.l[2];
        if (direction == 8)
            send_host_action(HOST_MOVE, 0, 0);
        else if (direction >= 0 && direction < 8)
            send_host_action(HOST_RESIZE, edges[direction], 0);
    } else if (event->message_type == atoms[NET_WM_STATE] &&
               ((Atom)event->data.l[1] == atoms[NET_WM_STATE_MAXIMIZED_HORZ] ||
                (Atom)event->data.l[1] == atoms[NET_WM_STATE_MAXIMIZED_VERT] ||
                (Atom)event->data.l[2] == atoms[NET_WM_STATE_MAXIMIZED_HORZ] ||
                (Atom)event->data.l[2] == atoms[NET_WM_STATE_MAXIMIZED_VERT])) {
        /* EWMH actions: remove=0, add=1, toggle=2. */
        maximized = event->data.l[0] == 2 ? !maximized : event->data.l[0] == 1;
        publish_maximized_state();
        send_host_action(maximized ? HOST_MAXIMIZE : HOST_RESTORE, 0, 0);
    } else if (event->message_type == atoms[WM_CHANGE_STATE] && event->data.l[0] == IconicState) {
        send_host_action(HOST_MINIMIZE, 0, 0);
    }
}

static void handle_property_notify(const XPropertyEvent *event)
{
    if (event->window == root_window && event->atom == atoms[HOST_STATE]) {
        sync_host_state();
    } else if (event->window == client_window) {
        if (event->atom == XA_WM_NAME || event->atom == atoms[NET_WM_NAME])
            sync_title();
        if (event->atom == atoms[MOTIF_WM_HINTS])
            sync_chrome();
        if (event->atom == XA_WM_NORMAL_HINTS)
            sync_minimum_size();
    }
}

static void advertise_window_manager(void)
{
    Atom supported[] = {
        atoms[NET_WM_STATE], atoms[NET_WM_STATE_MAXIMIZED_HORZ], atoms[NET_WM_STATE_MAXIMIZED_VERT],
        atoms[NET_WM_MOVERESIZE], atoms[NET_SUPPORTING_WM_CHECK],
        atoms[NET_ACTIVE_WINDOW],
    };
    XChangeProperty(display, root_window, atoms[NET_SUPPORTED], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)supported, 6);
    Window check = XCreateSimpleWindow(display, root_window, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(display, root_window, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(display, check, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&check, 1);
    const char *name = "LG3D";
    XChangeProperty(display, check, atoms[NET_WM_NAME], atoms[UTF8_STRING], 8,
                    PropModeReplace, (unsigned char *)name, strlen(name));
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    display = XOpenDisplay(NULL);
    if (!display) {
        fputs("Unable to open X11 display\n", stderr);
        return 1;
    }
    root_window = DefaultRootWindow(display);
    read_root_geometry();
    XInternAtoms(display, atom_names, ATOM_COUNT, False, atoms);
    advertise_window_manager();
    XSelectInput(display, root_window,
                 SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask);
    XSync(display, False);

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        switch (event.type) {
        case MapRequest:
            handle_map_request(&event.xmaprequest);
            break;
        case ConfigureRequest:
            handle_configure_request(&event.xconfigurerequest);
            break;
        case ClientMessage:
            handle_client_message(&event.xclient);
            break;
        case ButtonPress:
            handle_button_press(&event.xbutton);
            break;
        case ConfigureNotify:
            if (event.xconfigure.window == root_window) {
                read_root_geometry();
                fprintf(stderr, "ROOT %dx%d\n", root_width, root_height);
                fit_client_to_host();
            }
            break;
        case PropertyNotify:
            handle_property_notify(&event.xproperty);
            break;
        case DestroyNotify:
            if (event.xdestroywindow.window == client_window) {
                client_window = None;
                puts("CLIENT closed");
            }
            break;
        }
        XFlush(display);
    }
}
