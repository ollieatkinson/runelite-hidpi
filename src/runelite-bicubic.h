#ifndef RUNELITE_BICUBIC_H
#define RUNELITE_BICUBIC_H

#include <X11/Xdefs.h>
#include "pixmap.h"

struct xwl_window;
struct wl_buffer;

Bool runelite_bicubic_attach(struct xwl_window *window);
void runelite_bicubic_fini(struct xwl_window *window);
void runelite_bicubic_size(struct xwl_window *window, PixmapPtr source, int *width, int *height);
void runelite_bicubic_present(struct xwl_window *window, PixmapPtr source,
                             struct wl_buffer *buffer, int width, int height);
void runelite_bicubic_reset(struct xwl_window *window);

#endif
