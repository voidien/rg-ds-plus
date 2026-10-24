/*
 * test_scene.c - headless check of scene.c: renders both panels into pbuffers with whatever EGL/GLES
 * Mesa provides (software llvmpipe works) and verifies the picture, so the GL half of the example is
 * tested without a display. Run:  make test
 *
 * Checks: shaders compile and link; each panel is filled; the two panels are tinted differently;
 * the disc at t=0 is centred on the seam, so it is white at the top panel's bottom edge and at the
 * bottom panel's top edge at the same x.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scene.h"

#define W 1024
#define H 768

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static unsigned char px(const unsigned char *img, int x, int y_from_bottom, int c)
{
    return img[((size_t)y_from_bottom * W + x) * 4 + c];
}

int main(void)
{
    /* surfaceless-capable display, falling back to the default one */
    EGLDisplay dpy = EGL_NO_DISPLAY;
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (gpd)
        dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY)
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min))
        return fail("eglInitialize");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    EGLConfig cfg; EGLint n;
    if (!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1)
        return fail("no pbuffer EGLConfig");
    const EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(dpy, cfg, pa);
    const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
    if (s == EGL_NO_SURFACE || c == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, s, s, c))
        return fail("context");
    printf("EGL %d.%d, GL renderer: %s\n", maj, min, (const char *)glGetString(GL_RENDERER));

    if (scene_init())
        return fail("scene_init (shader compile/link)");

    unsigned char *img[2];
    for (int panel = 0; panel < 2; panel++) {
        glClearColor(1, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        scene_draw(panel, W, H, 0.0f);
        img[panel] = malloc((size_t)W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, img[panel]);
        if (glGetError() != GL_NO_ERROR)
            return fail("GL error");
        /* every pixel painted: the clear colour (magenta, g==0) must not survive */
        for (size_t i = 0; i < (size_t)W * H; i++)
            if (img[panel][i * 4 + 1] == 0 && img[panel][i * 4] == 255 && img[panel][i * 4 + 2] == 255)
                return fail("unpainted pixel");
    }

    /* tint: top (warm) is redder than blue, bottom (cool) the reverse, sampled away from the disc */
    int x = 100, y = 400;
    if (!(px(img[0], x, y, 0) > px(img[0], x, y, 2))) return fail("top panel not warm");
    if (!(px(img[1], x, y, 2) > px(img[1], x, y, 0))) return fail("bottom panel not cool");

    /* seam: GL row 0 is a panel's lower edge, row H-1 its upper edge */
    int top_edge = px(img[0], W / 2, 0, 0) + px(img[0], W / 2, 0, 1) + px(img[0], W / 2, 0, 2);
    int bot_edge = px(img[1], W / 2, H - 1, 0) + px(img[1], W / 2, H - 1, 1) + px(img[1], W / 2, H - 1, 2);
    printf("seam brightness: top panel lower edge %d, bottom panel upper edge %d (max 765)\n", top_edge, bot_edge);
    if (top_edge < 720 || bot_edge < 720)
        return fail("disc not centred on the seam");

    scene_fini();
    printf("PASS\n");
    return 0;
}
