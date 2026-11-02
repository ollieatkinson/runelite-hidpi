#ifndef RUNELITE_BICUBIC_GPU_H
#define RUNELITE_BICUBIC_GPU_H

struct xwl_screen;
struct xwl_window;

enum runelite_gpu_result {
    RUNELITE_GPU_FALLBACK,
    RUNELITE_GPU_BUSY,
    RUNELITE_GPU_ATTACHED,
};

enum runelite_gpu_result runelite_filter_attach(struct xwl_window *window);
void runelite_filter_fini(struct xwl_screen *screen);

#endif
