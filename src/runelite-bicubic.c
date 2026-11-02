/* Opt-in Pixman/SHM prototype: backing -> monitor scale. */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>

#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "xwayland-pixmap.h"
#include "runelite-bicubic.h"
#include "runelite-bicubic-gpu.h"
#include "viewporter-client-protocol.h"

#include "xwayland-shm.h"
struct runelite_frame { struct xorg_list link; PixmapPtr pixmap; };

static pixman_format_code_t runelite_bicubic_format(ScreenPtr screen, int depth)
{
    for (int i = 0; i < screen->numVisuals; i++) {
        VisualPtr v = &screen->visuals[i];
        if (v->vid != screen->rootVisual || v->class != TrueColor) continue;
        int r = __builtin_popcountl(v->redMask), g = __builtin_popcountl(v->greenMask), b = __builtin_popcountl(v->blueMask);
        Bool rgb = v->redMask > v->blueMask;
        unsigned long low = rgb ? v->blueMask : v->redMask, high = rgb ? v->redMask : v->blueMask;
        int first = rgb ? b : r, last = rgb ? r : b;
        if (low != (1UL << first)-1 || v->greenMask != ((1UL << g)-1) << first ||
            high != ((1UL << last)-1) << (first+g)) return 0;
        pixman_format_code_t format = PIXMAN_FORMAT(BitsPerPixel(depth), rgb ? PIXMAN_TYPE_ARGB : PIXMAN_TYPE_ABGR,
                                                    depth == 32 ? 32-r-g-b : 0, r, g, b);
        return pixman_format_supported_source(format) ? format : 0;
    }
    return 0;
}

static void runelite_bicubic_release(void *data)
{
    struct runelite_frame *frame = data;
    xorg_list_del(&frame->link);
    if (frame->pixmap) xwl_shm_destroy_pixmap(frame->pixmap);
    free(frame);
}

Bool runelite_bicubic_attach(struct xwl_window *w)
{
    struct xwl_screen *s = w->xwl_screen;
    ScreenPtr screen = s->screen;
    PixmapPtr source = screen->GetWindowPixmap(w->surface_window);
    struct runelite_frame *frame = NULL;
    pixman_image_t *input = NULL, *output = NULL;
    pixman_fixed_t *filter = NULL;
    pixman_transform_t transform;
    uint32_t *pixels = NULL;
    int width, height, count, stride;
    pixman_format_code_t format = runelite_bicubic_format(screen, source->drawable.depth);
    int depth = PIXMAN_FORMAT_A(format) ? 32 : 24;
    Bool ok = FALSE;
    if (!getenv("RUNELITE_HIDPI_BICUBIC") || s->rootless || !s->hidpi || s->fullscreen)
        return FALSE;
    if (!w->runelite_scale ||
        !s->viewporter || !w->fractional_scale || !format)
        goto done;
    if (runelite_filter_attach(w) == RUNELITE_GPU_ATTACHED)
        return TRUE;
    double ratio = w->runelite_scale / (120.0 * s->global_surface_scale);
    runelite_bicubic_size(w, source, &width, &height);
    stride = PixmapBytePad(source->drawable.width, source->drawable.depth);
    pixels = malloc((size_t)stride * source->drawable.height);
    frame = calloc(1, sizeof(*frame));
    if (!frame) goto done;
    xorg_list_append(&frame->link, &w->runelite_frames);
    frame->pixmap = xwl_shm_create_pixmap(screen, width, height, depth, CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    if (!pixels || !frame->pixmap) goto done;
    screen->GetImage(&source->drawable, 0, 0, source->drawable.width,
                     source->drawable.height, ZPixmap, ~0UL, (char *)pixels);
    input = pixman_image_create_bits(format, source->drawable.width,
                                    source->drawable.height, pixels, stride);
    output = pixman_image_create_bits(depth == 32 ? PIXMAN_a8r8g8b8 : PIXMAN_x8r8g8b8, width, height,
                                     frame->pixmap->devPrivate.ptr, frame->pixmap->devKind);
    pixman_fixed_t scale = pixman_double_to_fixed(1.0 / ratio);
    filter = pixman_filter_create_separable_convolution(&count, scale, scale,
        PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_BOX, PIXMAN_KERNEL_BOX, 4, 4);
    pixman_transform_init_scale(&transform, scale, scale);
    if (!input || !output || !filter || !pixman_image_set_transform(input, &transform) ||
        !pixman_image_set_filter(input, PIXMAN_FILTER_SEPARABLE_CONVOLUTION, filter, count) ||
        !xwl_pixmap_set_buffer_release_cb(frame->pixmap, runelite_bicubic_release, frame)) goto done;
    pixman_image_set_repeat(input, PIXMAN_REPEAT_PAD);
    pixman_image_composite32(PIXMAN_OP_SRC, input, NULL, output, 0, 0, 0, 0, 0, 0, width, height);
    if (!w->runelite_viewport)
        ErrorF("RuneLite bicubic: CPU, using monitor preferred scale\n");
    runelite_bicubic_present(w, source, xwl_shm_pixmap_get_wl_buffer(frame->pixmap), width, height);
    ok = TRUE;
done:
    if (input) pixman_image_unref(input);
    if (output) pixman_image_unref(output);
    free(filter);
    free(pixels);
    if (!ok && frame) runelite_bicubic_release(frame);
    if (!ok) runelite_bicubic_reset(w);
    return ok;
}

void runelite_bicubic_size(struct xwl_window *w, PixmapPtr source, int *width, int *height)
{
    /* Wayland reports scale in units of 1/120, not percent. */
    double ratio = w->runelite_scale / (120.0 * w->xwl_screen->global_surface_scale);
    *width = max(1, (int) lround(source->drawable.width * ratio));
    *height = max(1, (int) lround(source->drawable.height * ratio));
}

void runelite_bicubic_present(struct xwl_window *w, PixmapPtr source,
                             struct wl_buffer *buffer, int width, int height)
{
    struct xwl_screen *s = w->xwl_screen;
    if (!w->runelite_viewport)
        w->runelite_viewport = wp_viewporter_get_viewport(s->viewporter, w->surface);
    wp_viewport_set_destination(w->runelite_viewport,
        source->drawable.width / s->global_surface_scale,
        source->drawable.height / s->global_surface_scale);
    wl_surface_set_buffer_scale(w->surface, 1);
    wl_surface_attach(w->surface, buffer, 0, 0);
    wl_surface_damage_buffer(w->surface, 0, 0, width, height);
}

void runelite_bicubic_reset(struct xwl_window *w)
{
    if (w->runelite_viewport) {
        wp_viewport_destroy(w->runelite_viewport);
        w->runelite_viewport = NULL;
    }
    wl_surface_set_buffer_scale(w->surface, w->xwl_screen->global_surface_scale);
}

void runelite_bicubic_fini(struct xwl_window *w)
{
    struct runelite_frame *frame, *next;
    if (!w->xwl_screen->rootless)
        runelite_filter_fini(w->xwl_screen);
    xorg_list_for_each_entry_safe(frame, next, &w->runelite_frames, link)
        runelite_bicubic_release(frame);
    if (w->runelite_viewport) {
        wp_viewport_destroy(w->runelite_viewport);
        w->runelite_viewport = NULL;
    }
}
