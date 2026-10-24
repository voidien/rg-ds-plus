/*
 * kms_dualscreen.c - drive both RG DS Plus panels straight through DRM/KMS, no compositor.
 *
 * Method (as BananaLinux's ui/src/kms.cpp does it, written independently here):
 *   1. open the DRM card that has two connected DSI connectors,
 *   2. give each connector its own CRTC (VOP video port),
 *   3. make two dumb XRGB8888 buffers per panel, set the mode, then page-flip on vblank.
 * "DSI-2" is the top panel and "DSI-1" the bottom one (DSI controllers fe070000 / fe060000).
 *
 * Needs the display to be free: on the stock firmware stop Weston first (`/etc/init.d/S49weston stop`),
 * on BananaLinux stop banana-ui. Run as root. Exits by itself after SECONDS (default 10).
 *
 *   build: gcc -O2 -Wall -o kms_dualscreen kms_dualscreen.c $(pkg-config --cflags --libs libdrm)
 *   usage: ./kms_dualscreen [seconds]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct buf { uint32_t handle, fb, pitch; uint64_t size; uint32_t *map; };
struct panel {
    const char *name;
    uint32_t conn, crtc;
    drmModeModeInfo mode;
    struct buf b[2];
    int front;
    int flip_pending;
};

static int make_buf(int fd, int w, int h, struct buf *b)
{
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c))
        return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;
    if (drmModeAddFB(fd, w, h, 24, 32, b->pitch, b->handle, &b->fb))
        return -1;
    struct drm_mode_map_dumb m = { .handle = b->handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m))
        return -1;
    void *p = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (p == MAP_FAILED)
        return -1;
    b->map = p;
    memset(b->map, 0, b->size);
    return 0;
}

static void flip_done(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
    (void)fd; (void)seq; (void)sec; (void)usec;
    ((struct panel *)data)->flip_pending = 0;
}

/* a vertical gradient in the panel's colour with a white bar sweeping across */
static void draw(struct panel *p, struct buf *b, int t, uint32_t base)
{
    int w = p->mode.hdisplay, h = p->mode.vdisplay;
    int bar = t % w;
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)b->map + (size_t)y * b->pitch);
        uint32_t shade = 255 - (uint32_t)(y * 160 / h);
        uint32_t r = ((base >> 16) & 255) * shade / 255, g = ((base >> 8) & 255) * shade / 255,
                 bl = (base & 255) * shade / 255;
        uint32_t px = (r << 16) | (g << 8) | bl;
        for (int x = 0; x < w; x++)
            row[x] = (x >= bar && x < bar + 24) ? 0xffffff : px;
    }
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    int fd = -1;
    struct panel p[2] = { { .name = "DSI-2 (top)" }, { .name = "DSI-1 (bottom)" } };
    drmModeRes *res = NULL;

    for (int card = 0; card < 4 && fd < 0; card++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%d", card);
        int f = open(path, O_RDWR | O_CLOEXEC);
        if (f < 0)
            continue;
        res = drmModeGetResources(f);
        if (!res) { close(f); continue; }
        uint32_t used_crtc[2] = {0, 0};
        int found = 0;
        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *c = drmModeGetConnector(f, res->connectors[i]);
            if (!c) continue;
            if (c->connector_type == DRM_MODE_CONNECTOR_DSI && c->connection == DRM_MODE_CONNECTED &&
                c->count_modes > 0) {
                /* top = the connector whose type id is 2 ("DSI-2"), bottom = type id 1 */
                int idx = c->connector_type_id == 2 ? 0 : 1;
                uint32_t crtc = 0;
                for (int e = 0; e < c->count_encoders && !crtc; e++) {
                    drmModeEncoder *enc = drmModeGetEncoder(f, c->encoders[e]);
                    if (!enc) continue;
                    for (int k = 0; k < res->count_crtcs && !crtc; k++)
                        if ((enc->possible_crtcs & (1u << k)) && res->crtcs[k] != used_crtc[0] &&
                            res->crtcs[k] != used_crtc[1])
                            crtc = res->crtcs[k];
                    drmModeFreeEncoder(enc);
                }
                if (crtc && !p[idx].conn) {
                    p[idx].conn = c->connector_id;
                    p[idx].crtc = used_crtc[idx] = crtc;
                    p[idx].mode = c->modes[0];
                    for (int m = 0; m < c->count_modes; m++)
                        if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { p[idx].mode = c->modes[m]; break; }
                    found++;
                }
            }
            drmModeFreeConnector(c);
        }
        if (found == 2) fd = f; else { drmModeFreeResources(res); close(f); }
    }
    if (fd < 0) {
        fprintf(stderr, "no DRM device with two connected DSI panels (is Weston/banana-ui still running?)\n");
        return 1;
    }
    drmModeFreeResources(res);
    drmSetMaster(fd);

    static const uint32_t colour[2] = { 0xd04040, 0x4060d0 };
    for (int s = 0; s < 2; s++) {
        printf("%s: connector %u crtc %u mode %dx%d@%u\n", p[s].name, p[s].conn, p[s].crtc,
               p[s].mode.hdisplay, p[s].mode.vdisplay, p[s].mode.vrefresh);
        for (int i = 0; i < 2; i++)
            if (make_buf(fd, p[s].mode.hdisplay, p[s].mode.vdisplay, &p[s].b[i])) {
                fprintf(stderr, "buffer: %s\n", strerror(errno));
                return 1;
            }
        draw(&p[s], &p[s].b[0], 0, colour[s]);
        if (drmModeSetCrtc(fd, p[s].crtc, p[s].b[0].fb, 0, 0, &p[s].conn, 1, &p[s].mode)) {
            fprintf(stderr, "SetCrtc %s: %s (display master busy?)\n", p[s].name, strerror(errno));
            return 1;
        }
    }

    drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int frame = 0;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec >= seconds)
            break;
        for (int s = 0; s < 2; s++) {
            if (p[s].flip_pending) continue;
            int back = p[s].front ^ 1;
            draw(&p[s], &p[s].b[back], frame * 6, colour[s]);
            if (drmModePageFlip(fd, p[s].crtc, p[s].b[back].fb, DRM_MODE_PAGE_FLIP_EVENT, &p[s]) == 0) {
                p[s].flip_pending = 1;
                p[s].front = back;
            }
        }
        frame++;
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll(&pf, 1, 100) > 0)
            drmHandleEvent(fd, &ev);
    }
    printf("%d frames\n", frame);
    drmDropMaster(fd);
    close(fd);
    return 0;
}
