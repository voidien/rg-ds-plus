/*
 * gbm_egl_dualscreen.c - GPU-rendered output on both RG DS Plus panels: GBM + EGL + OpenGL ES 2,
 * straight to KMS with no compositor. Meant for a Mesa/Panfrost system such as BananaLinux or a
 * custom Buildroot image (Mesa built with EGL on DRM/GBM and GLES). The stock firmware's
 * libEGL/libgbm/libGLESv2 are symlinks to one Rockchip Mali blob (libmali-bifrost-g52-g24p0-
 * wayland-gbm.so), so this may also run there with Weston stopped; untested.
 *
 * Per panel: one gbm_surface (XRGB8888, SCANOUT | RENDERING) -> one EGL window surface. A single
 * EGL context and GBM device serve both. Each frame: draw -> eglSwapBuffers -> lock the front
 * buffer -> wrap it in a DRM framebuffer (cached per buffer object) -> page flip; the buffer shown
 * before is released when the flip event arrives.
 *
 * "DSI-2" is the top panel, "DSI-1" the bottom (DSI controllers fe070000 / fe060000).
 * The display must be free: stop Weston / banana-ui first, run as root.
 *
 *   build: make          usage: ./gbm_egl_dualscreen [seconds]
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "scene.h"

#define FMT GBM_FORMAT_XRGB8888

struct panel {
    const char *name;
    uint32_t conn, crtc;
    drmModeModeInfo mode;
    struct gbm_surface *gs;
    EGLSurface es;
    struct gbm_bo *shown, *pending;     /* bo on screen; bo whose flip is in flight */
    int flip_pending;
};

static int fd = -1;
static struct gbm_device *gbm;
static EGLDisplay dpy;
static EGLContext ctx;
static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
static PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_window_surface;

static int find_panels(struct panel p[2])
{
    for (int card = 0; card < 4; card++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%d", card);
        int f = open(path, O_RDWR | O_CLOEXEC);
        if (f < 0)
            continue;
        drmModeRes *res = drmModeGetResources(f);
        if (!res) { close(f); continue; }
        uint32_t used[2] = {0, 0};
        int found = 0;
        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *c = drmModeGetConnector(f, res->connectors[i]);
            if (!c) continue;
            if (c->connector_type == DRM_MODE_CONNECTOR_DSI && c->connection == DRM_MODE_CONNECTED &&
                c->count_modes > 0) {
                int idx = c->connector_type_id == 2 ? 0 : 1;   /* DSI-2 top, DSI-1 bottom */
                uint32_t crtc = 0;
                for (int e = 0; e < c->count_encoders && !crtc; e++) {
                    drmModeEncoder *enc = drmModeGetEncoder(f, c->encoders[e]);
                    if (!enc) continue;
                    for (int k = 0; k < res->count_crtcs && !crtc; k++)
                        if ((enc->possible_crtcs & (1u << k)) && res->crtcs[k] != used[0] &&
                            res->crtcs[k] != used[1])
                            crtc = res->crtcs[k];
                    drmModeFreeEncoder(enc);
                }
                if (crtc && !p[idx].conn) {
                    p[idx].conn = c->connector_id;
                    p[idx].crtc = used[idx] = crtc;
                    p[idx].mode = c->modes[0];
                    for (int m = 0; m < c->count_modes; m++)
                        if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { p[idx].mode = c->modes[m]; break; }
                    found++;
                }
            }
            drmModeFreeConnector(c);
        }
        drmModeFreeResources(res);
        if (found == 2) { fd = f; return 0; }
        close(f);
        p[0].conn = p[1].conn = 0;      /* try the next card with a clean slate */
    }
    return -1;
}

/* An EGLConfig whose native visual is the GBM format we scan out. */
static EGLConfig pick_config(void)
{
    static const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE };
    EGLint n = 0;
    if (!eglGetConfigs(dpy, NULL, 0, &n) || n < 1)
        return NULL;
    EGLConfig *cfgs = malloc(sizeof *cfgs * n);
    EGLConfig found = NULL;
    if (eglChooseConfig(dpy, attrs, cfgs, n, &n)) {
        for (int i = 0; i < n && !found; i++) {
            EGLint id;
            if (eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &id) && (uint32_t)id == FMT)
                found = cfgs[i];
        }
    }
    free(cfgs);
    return found;
}

/* The DRM framebuffer for a bo, made once and kept as the bo's user data. */
static void fb_destroy(struct gbm_bo *bo, void *data)
{
    uint32_t id = (uint32_t)(uintptr_t)data;
    drmModeRmFB(gbm_device_get_fd(gbm_bo_get_device(bo)), id);
}

static uint32_t fb_for(struct gbm_bo *bo)
{
    void *d = gbm_bo_get_user_data(bo);
    if (d)
        return (uint32_t)(uintptr_t)d;
    uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 }, strides[4] = { gbm_bo_get_stride(bo) },
             offsets[4] = { 0 }, id = 0;
    if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), FMT, handles, strides, offsets, &id, 0))
        return 0;
    gbm_bo_set_user_data(bo, (void *)(uintptr_t)id, fb_destroy);
    return id;
}

static void flip_done(int f, unsigned seq, unsigned sec, unsigned usec, void *data)
{
    (void)f; (void)seq; (void)sec; (void)usec;
    struct panel *p = data;
    if (p->shown)
        gbm_surface_release_buffer(p->gs, p->shown);
    p->shown = p->pending;
    p->pending = NULL;
    p->flip_pending = 0;
}

static int draw_and_lock(struct panel *p, int idx, float t, struct gbm_bo **bo)
{
    if (!eglMakeCurrent(dpy, p->es, p->es, ctx))
        return -1;
    scene_draw(idx, p->mode.hdisplay, p->mode.vdisplay, t);
    if (!eglSwapBuffers(dpy, p->es))
        return -1;
    *bo = gbm_surface_lock_front_buffer(p->gs);
    return *bo ? 0 : -1;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    struct panel p[2] = { { .name = "DSI-2 (top)" }, { .name = "DSI-1 (bottom)" } };

    if (find_panels(p)) {
        fprintf(stderr, "no DRM device with two connected DSI panels (Weston / banana-ui still running?)\n");
        return 1;
    }
    drmSetMaster(fd);
    gbm = gbm_create_device(fd);
    if (!gbm) { fprintf(stderr, "gbm_create_device failed\n"); return 1; }

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    create_window_surface = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    if (!get_platform_display || !create_window_surface) {
        fprintf(stderr, "EGL_EXT_platform_base missing\n");
        return 1;
    }
    dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    EGLint major, minor;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed (0x%x)\n", eglGetError());
        return 1;
    }
    printf("EGL %d.%d, vendor %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg = pick_config();
    if (!cfg) { fprintf(stderr, "no EGLConfig matches XRGB8888 scanout\n"); return 1; }
    static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext: 0x%x\n", eglGetError()); return 1; }

    for (int s = 0; s < 2; s++) {
        p[s].gs = gbm_surface_create(gbm, p[s].mode.hdisplay, p[s].mode.vdisplay, FMT,
                                     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!p[s].gs) { fprintf(stderr, "gbm_surface_create(%s) failed\n", p[s].name); return 1; }
        p[s].es = create_window_surface(dpy, cfg, p[s].gs, NULL);
        if (p[s].es == EGL_NO_SURFACE) { fprintf(stderr, "window surface: 0x%x\n", eglGetError()); return 1; }
    }
    eglMakeCurrent(dpy, p[0].es, p[0].es, ctx);
    eglSwapInterval(dpy, 0);            /* pacing comes from the KMS page flips, not from EGL */
    printf("GL renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    if (scene_init()) { fprintf(stderr, "scene_init failed\n"); return 1; }

    /* first frame of each panel: draw, then set the CRTC to show it */
    for (int s = 0; s < 2; s++) {
        struct gbm_bo *bo;
        if (draw_and_lock(&p[s], s, 0.0f, &bo)) { fprintf(stderr, "first frame (%s) failed\n", p[s].name); return 1; }
        uint32_t fb = fb_for(bo);
        if (!fb || drmModeSetCrtc(fd, p[s].crtc, fb, 0, 0, &p[s].conn, 1, &p[s].mode)) {
            fprintf(stderr, "SetCrtc %s: %s\n", p[s].name, strerror(errno));
            return 1;
        }
        p[s].shown = bo;
        printf("%s: %dx%d@%u\n", p[s].name, p[s].mode.hdisplay, p[s].mode.vdisplay, p[s].mode.vrefresh);
    }

    drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long frames[2] = {0, 0};
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        float t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9f;
        if (t >= seconds)
            break;
        for (int s = 0; s < 2; s++) {
            if (p[s].flip_pending)
                continue;
            struct gbm_bo *bo;
            if (draw_and_lock(&p[s], s, t, &bo))
                continue;
            uint32_t fb = fb_for(bo);
            if (fb && drmModePageFlip(fd, p[s].crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, &p[s]) == 0) {
                p[s].pending = bo;
                p[s].flip_pending = 1;
                frames[s]++;
            } else {
                gbm_surface_release_buffer(p[s].gs, bo);
            }
        }
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll(&pf, 1, 100) > 0)
            drmHandleEvent(fd, &ev);
    }
    printf("frames: top %ld, bottom %ld in %d s\n", frames[0], frames[1], seconds);

    /* wait for flips in flight, then tear down in reverse order */
    for (int tries = 0; tries < 20 && (p[0].flip_pending || p[1].flip_pending); tries++) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll(&pf, 1, 100) > 0)
            drmHandleEvent(fd, &ev);
    }
    scene_fini();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    for (int s = 0; s < 2; s++) {
        eglDestroySurface(dpy, p[s].es);
        if (p[s].shown) gbm_surface_release_buffer(p[s].gs, p[s].shown);
        gbm_surface_destroy(p[s].gs);
    }
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    drmDropMaster(fd);
    close(fd);
    return 0;
}
