/* Optional GPU bicubic for the private rootful HiDPI surface.
 * X coordinates retain the integer backing scale; only submitted buffers shrink.
 * Three DMA-BUF pixmaps are reused only after wl_buffer.release. The caller
 * retains CPU bicubic when GPU filtering cannot be used. */
#include <epoxy/gl.h>
#include "glamor.h"
#include "xwayland-glamor.h"
#include "runelite-bicubic-shader.h"

static Bool runelite_gpu_failed;
static GLuint runelite_program, runelite_vao, runelite_fbo, runelite_intermediate;
static int runelite_intermediate_width, runelite_intermediate_height;
static pixman_fixed_t runelite_filter_scale;
static int runelite_filter_taps;
static GLfloat runelite_filter_weights[256];
static struct { PixmapPtr pixmap; Bool busy; } runelite_outputs[3];

static void
runelite_filter_fini(struct xwl_screen *s)
{
    if (!s->glamor)
        return;
    xwl_glamor_egl_make_current(s);
    for (int i = 0; i < ARRAY_SIZE(runelite_outputs); i++) {
        if (runelite_outputs[i].pixmap)
            s->screen->DestroyPixmap(runelite_outputs[i].pixmap);
        runelite_outputs[i].pixmap = NULL;
        runelite_outputs[i].busy = FALSE;
    }
    if (runelite_program) glDeleteProgram(runelite_program);
    if (runelite_vao) glDeleteVertexArrays(1, &runelite_vao);
    if (runelite_fbo) glDeleteFramebuffers(1, &runelite_fbo);
    if (runelite_intermediate) glDeleteTextures(1, &runelite_intermediate);
    runelite_program = runelite_vao = runelite_fbo = runelite_intermediate = 0;
    runelite_intermediate_width = runelite_intermediate_height = 0;
    runelite_filter_scale = 0;
    runelite_gpu_failed = FALSE;
}

static Bool
runelite_filter_requested(void)
{
    const char *value = getenv("RUNELITE_HIDPI_BICUBIC_GPU");
    return value && strcmp(value, "1") == 0 && !runelite_gpu_failed;
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
    if (runelite_program)
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
    runelite_program = glCreateProgram();
    glAttachShader(runelite_program, vertex);
    glAttachShader(runelite_program, fragment);
    glLinkProgram(runelite_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glGetProgramiv(runelite_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(runelite_program, sizeof(log), NULL, log);
        ErrorF("RuneLite filter link: %s\n", log);
        glDeleteProgram(runelite_program);
        runelite_program = 0;
        return FALSE;
    }
    glGenVertexArrays(1, &runelite_vao);
    glGenFramebuffers(1, &runelite_fbo);
    glGenTextures(1, &runelite_intermediate);
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
    glUniform1f(glGetUniformLocation(runelite_program, "sourceScale"),
                pixman_fixed_to_double(runelite_filter_scale));
    glUniform1i(glGetUniformLocation(runelite_program, "taps"), runelite_filter_taps);
    glUniform1fv(glGetUniformLocation(runelite_program, "coefficients"),
                 runelite_filter_taps * 16, runelite_filter_weights);
    glUniform1i(glGetUniformLocation(runelite_program, "axis"), axis);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

/* Return 1 when attached, 0 when all buffers are busy, -1 for CPU fallback. */
static int
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
        return -1;
    if (!w->runelite_scale) return -1;
    if (!s->glamor || !s->viewporter || !w->fractional_scale ||
        !xwl_glamor_supports_implicit_sync(s))
        goto fail;
    /* Above the backing scale, leave upscaling to the compositor. */
    if (w->runelite_scale > s->global_surface_scale * 120) {
        /* preferred_scale can arrive before output.enter updates the integer
         * scale. Fall back temporarily, and retry on subsequent damage. */
        if (w->runelite_viewport) {
            wp_viewport_destroy(w->runelite_viewport);
            w->runelite_viewport = NULL;
        }
        wl_surface_set_buffer_scale(w->surface, s->global_surface_scale);
        return -1;
    }
    source = screen->GetWindowPixmap(w->surface_window);
    if (source->drawable.depth != 24 && source->drawable.depth != 32) return -1;
    width = max(1, (int) lround((double) source->drawable.width * w->runelite_scale /
                              (120 * s->global_surface_scale)));
    height = max(1, (int) lround((double) source->drawable.height * w->runelite_scale /
                               (120 * s->global_surface_scale)));
    for (slot = 0; slot < ARRAY_SIZE(runelite_outputs); slot++)
        if (!runelite_outputs[slot].busy)
            break;
    if (slot == ARRAY_SIZE(runelite_outputs))
        return 0;
    pixman_fixed_t scale = pixman_double_to_fixed(120.0 * s->global_surface_scale / w->runelite_scale);
    if (scale != runelite_filter_scale) {
        int count;
        pixman_fixed_t *filter = pixman_filter_create_separable_convolution(&count, scale, scale,
            PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_CUBIC, PIXMAN_KERNEL_BOX, PIXMAN_KERNEL_BOX, 4, 4);
        if (!filter) goto fail;
        int taps = pixman_fixed_to_int(filter[0]);
        if (taps < 1 || taps > 16) { free(filter); goto fail; }
        for (int i = 0; i < taps * 16; ++i)
            runelite_filter_weights[i] = pixman_fixed_to_double(filter[4+i]);
        free(filter);
        runelite_filter_taps = taps;
        runelite_filter_scale = scale;
    }
    xwl_glamor_egl_make_current(s);
    if (!runelite_filter_init())
        goto fail;
    target = runelite_outputs[slot].pixmap;
    if (target && (target->drawable.width != width || target->drawable.height != height)) {
        screen->DestroyPixmap(target);
        runelite_outputs[slot].pixmap = target = NULL;
    }
    if (!target) {
        target = screen->CreatePixmap(screen, width, height, source->drawable.depth,
                                      CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
        runelite_outputs[slot].pixmap = target;
        if (target)
            ErrorF("RuneLite filter %s: %dx%d -> %dx%d (buffer %d)\n",
                   "bicubic GPU", source->drawable.width,
                   source->drawable.height, width, height, slot);
    }
    if (!target || !xwl_glamor_pixmap_texture(source) || !xwl_glamor_pixmap_texture(target))
        goto fail;
    buffer = xwl_pixmap_get_wl_buffer(target);
    if (!buffer || !xwl_pixmap_set_buffer_release_cb(target, runelite_filter_release,
                                                     &runelite_outputs[slot].busy))
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
    glBindTexture(GL_TEXTURE_2D, runelite_intermediate);
    if (width != runelite_intermediate_width || source->drawable.height != runelite_intermediate_height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, source->drawable.height,
                     0, GL_RGBA, GL_HALF_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        runelite_intermediate_width = width;
        runelite_intermediate_height = source->drawable.height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, runelite_fbo);
    glUseProgram(runelite_program);
    glBindVertexArray(runelite_vao);
    glUniform1i(glGetUniformLocation(runelite_program, "image"), 0);
    ok = runelite_filter_pass(xwl_glamor_pixmap_texture(source), runelite_intermediate,
                              width, source->drawable.height, 0) &&
         runelite_filter_pass(runelite_intermediate, xwl_glamor_pixmap_texture(target),
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
    if (!w->runelite_viewport)
        w->runelite_viewport = wp_viewporter_get_viewport(s->viewporter, w->surface);
    wp_viewport_set_destination(w->runelite_viewport,
        source->drawable.width / s->global_surface_scale,
        source->drawable.height / s->global_surface_scale);
    wl_surface_set_buffer_scale(w->surface, 1);
    wl_surface_attach(w->surface, buffer, 0, 0);
    wl_surface_damage_buffer(w->surface, 0, 0, width, height);
    runelite_outputs[slot].busy = TRUE;
    return 1;
fail:
    ErrorF("RuneLite GPU filter unavailable; retaining CPU bicubic\n");
    runelite_gpu_failed = TRUE;
    if (w->runelite_viewport) {
        wp_viewport_destroy(w->runelite_viewport);
        w->runelite_viewport = NULL;
    }
    wl_surface_set_buffer_scale(w->surface, s->global_surface_scale);
    return -1;
}
