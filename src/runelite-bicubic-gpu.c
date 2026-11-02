/* Optional GPU bicubic for the private rootful HiDPI surface.
 * X coordinates retain the integer backing scale; only submitted buffers shrink.
 * Three DMA-BUF pixmaps are reused only after wl_buffer.release. The caller
 * retains CPU bicubic when GPU filtering cannot be used. */
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

#include <epoxy/gl.h>
#include "glamor.h"
#include "xwayland-glamor.h"
#include "bicubic-vert.h"
#include "bicubic-frag.h"

static struct {
    Bool failed;
    GLuint program, vao, fbo, intermediate;
    int intermediate_width, intermediate_height;
    pixman_fixed_t filter_scale;
    int filter_taps;
    GLfloat filter_weights[256];
    struct { PixmapPtr pixmap; Bool busy; } outputs[3];
    struct { GLint image, source_scale, taps, coefficients, axis; } uniforms;
} gpu;

void
runelite_filter_fini(struct xwl_screen *s)
{
    if (!s->glamor)
        return;
    xwl_glamor_egl_make_current(s);
    for (int i = 0; i < ARRAY_SIZE(gpu.outputs); i++) {
        if (gpu.outputs[i].pixmap)
            s->screen->DestroyPixmap(gpu.outputs[i].pixmap);
        gpu.outputs[i].pixmap = NULL;
        gpu.outputs[i].busy = FALSE;
    }
    if (gpu.program) glDeleteProgram(gpu.program);
    if (gpu.vao) glDeleteVertexArrays(1, &gpu.vao);
    if (gpu.fbo) glDeleteFramebuffers(1, &gpu.fbo);
    if (gpu.intermediate) glDeleteTextures(1, &gpu.intermediate);
    memset(&gpu, 0, sizeof(gpu));
}

static Bool
runelite_filter_requested(void)
{
    const char *value = getenv("RUNELITE_HIDPI_BICUBIC_GPU");
    return value && strcmp(value, "1") == 0 && !gpu.failed;
}

static GLuint
runelite_compile(GLenum type, const char *body)
{
    const char *parts[] = {epoxy_is_desktop_gl() ? "#version 130\n" :
        "#version 300 es\nprecision highp float;precision highp int;\n", body};
    GLuint shader = glCreateShader(type);
    GLint ok;
    glShaderSource(shader, 2, parts, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        ErrorF("RuneLite filter shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static Bool
runelite_filter_init(void)
{
    GLuint vertex, fragment;
    GLint ok;
    if (gpu.program)
        return TRUE;
    if (epoxy_gl_version() < 30)
        return FALSE;
    vertex = runelite_compile(GL_VERTEX_SHADER, runelite_filter_vertex);
    fragment = runelite_compile(GL_FRAGMENT_SHADER, runelite_filter_fragment);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return FALSE;
    }
    gpu.program = glCreateProgram();
    glAttachShader(gpu.program, vertex);
    glAttachShader(gpu.program, fragment);
    glLinkProgram(gpu.program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glGetProgramiv(gpu.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(gpu.program, sizeof(log), NULL, log);
        ErrorF("RuneLite filter link: %s\n", log);
        glDeleteProgram(gpu.program);
        gpu.program = 0;
        return FALSE;
    }
    gpu.uniforms.image = glGetUniformLocation(gpu.program, "image");
    gpu.uniforms.source_scale = glGetUniformLocation(gpu.program, "sourceScale");
    gpu.uniforms.taps = glGetUniformLocation(gpu.program, "taps");
    gpu.uniforms.coefficients = glGetUniformLocation(gpu.program, "coefficients");
    gpu.uniforms.axis = glGetUniformLocation(gpu.program, "axis");
    glGenVertexArrays(1, &gpu.vao);
    glGenFramebuffers(1, &gpu.fbo);
    glGenTextures(1, &gpu.intermediate);
    return TRUE;
}

static void
runelite_filter_release(void *data)
{
    Bool *busy = data;
    *busy = FALSE;
}

static Bool
runelite_filter_pass(GLuint source, GLuint target, int width, int height, int axis)
{
    glBindTexture(GL_TEXTURE_2D, source);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return FALSE;
    glViewport(0, 0, width, height);
    glUniform1f(gpu.uniforms.source_scale,
                pixman_fixed_to_double(gpu.filter_scale));
    glUniform1i(gpu.uniforms.taps, gpu.filter_taps);
    glUniform1fv(gpu.uniforms.coefficients,
                 gpu.filter_taps * 16, gpu.filter_weights);
    glUniform1i(gpu.uniforms.axis, axis);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

enum runelite_gpu_result
runelite_filter_attach(struct xwl_window *w)
{
    struct xwl_screen *s = w->xwl_screen;
    ScreenPtr screen = s->screen;
    PixmapPtr source, target;
    struct wl_buffer *buffer;
    int width, height, slot;
    GLint program, vao, draw_fbo, read_fbo, viewport[4], active, texture;
    GLboolean mask[4], enabled[5];
    const GLenum caps[] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_FRAMEBUFFER_SRGB};
    Bool ok;
    if (!runelite_filter_requested() || s->rootless || !s->hidpi || s->fullscreen)
        return RUNELITE_GPU_FALLBACK;
    if (!w->runelite_scale) return RUNELITE_GPU_FALLBACK;
    if (!s->glamor || !s->viewporter || !w->fractional_scale ||
        !xwl_glamor_supports_implicit_sync(s))
        goto fail;
    /* Above the backing scale, leave upscaling to the compositor. */
    if (w->runelite_scale > s->global_surface_scale * 120) {
        /* preferred_scale can arrive before output.enter updates the integer
         * scale. Fall back temporarily, and retry on subsequent damage. */
        runelite_bicubic_reset(w);
        return RUNELITE_GPU_FALLBACK;
    }
    source = screen->GetWindowPixmap(w->surface_window);
    if (source->drawable.depth != 24 && source->drawable.depth != 32) return RUNELITE_GPU_FALLBACK;
    runelite_bicubic_size(w, source, &width, &height);
    for (slot = 0; slot < ARRAY_SIZE(gpu.outputs); slot++)
        if (!gpu.outputs[slot].busy)
            break;
    if (slot == ARRAY_SIZE(gpu.outputs))
        return RUNELITE_GPU_BUSY;
    pixman_fixed_t scale = pixman_double_to_fixed(120.0 * s->global_surface_scale / w->runelite_scale);
    if (scale != gpu.filter_scale) {
        int count;
        pixman_fixed_t *filter = pixman_filter_create_separable_convolution(&count, scale, scale,
            PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_BOX, PIXMAN_KERNEL_BOX, 4, 4);
        if (!filter) goto fail;
        int taps = pixman_fixed_to_int(filter[0]);
        if (taps < 1 || taps > 16) { free(filter); goto fail; }
        for (int i = 0; i < taps * 16; ++i)
            gpu.filter_weights[i] = pixman_fixed_to_double(filter[4+i]);
        free(filter);
        gpu.filter_taps = taps;
        gpu.filter_scale = scale;
    }
    xwl_glamor_egl_make_current(s);
    if (!runelite_filter_init())
        goto fail;
    target = gpu.outputs[slot].pixmap;
    if (target && (target->drawable.width != width || target->drawable.height != height)) {
        screen->DestroyPixmap(target);
        gpu.outputs[slot].pixmap = target = NULL;
    }
    if (!target) {
        target = screen->CreatePixmap(screen, width, height, source->drawable.depth,
                                      CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
        gpu.outputs[slot].pixmap = target;
        if (target)
            ErrorF("RuneLite filter %s: %dx%d -> %dx%d (buffer %d)\n",
                   "bicubic GPU", source->drawable.width,
                   source->drawable.height, width, height, slot);
    }
    if (!target || !xwl_glamor_pixmap_texture(source) || !xwl_glamor_pixmap_texture(target))
        goto fail;
    buffer = xwl_pixmap_get_wl_buffer(target);
    if (!buffer || !xwl_pixmap_set_buffer_release_cb(target, runelite_filter_release,
                                                     &gpu.outputs[slot].busy))
        goto fail;

    /* Glamor caches some GL bindings. Restore the complete state we alter. */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    for (int i = 0; i < ARRAY_SIZE(caps); i++) {
        enabled[i] = (i == 4 && !epoxy_is_desktop_gl()) ? FALSE : glIsEnabled(caps[i]);
        if (i != 4 || epoxy_is_desktop_gl()) glDisable(caps[i]);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindTexture(GL_TEXTURE_2D, gpu.intermediate);
    if (width != gpu.intermediate_width || source->drawable.height != gpu.intermediate_height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, source->drawable.height,
                     0, GL_RGBA, GL_HALF_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gpu.intermediate_width = width;
        gpu.intermediate_height = source->drawable.height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gpu.fbo);
    glUseProgram(gpu.program);
    glBindVertexArray(gpu.vao);
    glUniform1i(gpu.uniforms.image, 0);
    ok = runelite_filter_pass(xwl_glamor_pixmap_texture(source), gpu.intermediate,
                              width, source->drawable.height, 0) &&
         runelite_filter_pass(gpu.intermediate, xwl_glamor_pixmap_texture(target),
                              width, height, 1);
    /* The experimental path uses implicit synchronization on its own GBM buffer. */
    glFlush();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    glUseProgram(program);
    glBindVertexArray(vao);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(active);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    for (int i = 0; i < ARRAY_SIZE(caps); i++)
        if (enabled[i]) glEnable(caps[i]);
    if (!ok)
        goto fail;
    runelite_bicubic_present(w, source, buffer, width, height);
    gpu.outputs[slot].busy = TRUE;
    return RUNELITE_GPU_ATTACHED;
fail:
    ErrorF("RuneLite GPU filter unavailable; retaining CPU bicubic\n");
    gpu.failed = TRUE;
    runelite_bicubic_reset(w);
    return RUNELITE_GPU_FALLBACK;
}
