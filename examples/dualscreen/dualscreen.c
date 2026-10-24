/*
 * dualscreen.c - minimal dual-screen SDL2 test for the Anbernic RG DS Plus.
 *
 * Stock firmware runs Weston with two DRM outputs (DSI-1, DSI-2); SDL2 on the
 * Wayland backend exposes them as two SDL displays. This opens one fullscreen
 * window per display and fills each with a different colour plus a moving bar,
 * so you can see which display index is the top screen and which is the bottom.
 *
 * Run from the launcher with:
 *   export XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=wayland
 *   ./dualscreen
 * Press any key / button (or the lid-close hall switch is ignored) to quit;
 * it also exits by itself after 20 seconds.
 */
#include <SDL2/SDL.h>
#include <stdio.h>

#define MAX_SCREENS 4

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int n = SDL_GetNumVideoDisplays();
    printf("video driver: %s, displays: %d\n", SDL_GetCurrentVideoDriver(), n);
    if (n > MAX_SCREENS)
        n = MAX_SCREENS;

    SDL_Window *win[MAX_SCREENS] = {0};
    SDL_Renderer *ren[MAX_SCREENS] = {0};
    static const Uint8 colours[MAX_SCREENS][3] = {
        {200, 40, 40}, {40, 90, 200}, {40, 160, 60}, {200, 170, 30}};
    int opened = 0;

    for (int i = 0; i < n; i++) {
        SDL_Rect b;
        if (SDL_GetDisplayBounds(i, &b) != 0) {
            fprintf(stderr, "display %d bounds: %s\n", i, SDL_GetError());
            continue;
        }
        printf("display %d (%s): %dx%d at %d,%d\n", i, SDL_GetDisplayName(i),
               b.w, b.h, b.x, b.y);
        /* Position the window on its display so the compositor puts it there. */
        win[i] = SDL_CreateWindow("dualscreen", b.x, b.y, b.w, b.h,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
        if (!win[i]) {
            fprintf(stderr, "window %d: %s\n", i, SDL_GetError());
            continue;
        }
        SDL_SetWindowFullscreen(win[i], SDL_WINDOW_FULLSCREEN_DESKTOP);
        ren[i] = SDL_CreateRenderer(win[i], -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren[i])
            ren[i] = SDL_CreateRenderer(win[i], -1, SDL_RENDERER_SOFTWARE);
        if (ren[i])
            opened++;
    }

    if (!opened) {
        fprintf(stderr, "no usable window\n");
        SDL_Quit();
        return 1;
    }

    Uint32 start = SDL_GetTicks();
    int running = 1;
    while (running && SDL_GetTicks() - start < 20000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_KEYDOWN ||
                e.type == SDL_JOYBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONDOWN)
                running = 0;
        }
        int t = (int)(SDL_GetTicks() / 4);
        for (int i = 0; i < n; i++) {
            if (!ren[i])
                continue;
            int w, h;
            SDL_GetRendererOutputSize(ren[i], &w, &h);
            SDL_SetRenderDrawColor(ren[i], colours[i][0], colours[i][1], colours[i][2], 255);
            SDL_RenderClear(ren[i]);
            /* Bar sweeps across; the digit-like block count tells the display index. */
            SDL_SetRenderDrawColor(ren[i], 255, 255, 255, 255);
            SDL_Rect bar = {t % (w > 40 ? w - 40 : 1), h / 2 - 20, 40, 40};
            SDL_RenderFillRect(ren[i], &bar);
            for (int k = 0; k <= i; k++) {
                SDL_Rect idx = {20 + k * 40, 20, 30, 30};
                SDL_RenderFillRect(ren[i], &idx);
            }
            SDL_RenderPresent(ren[i]);
        }
    }

    for (int i = 0; i < n; i++) {
        if (ren[i])
            SDL_DestroyRenderer(ren[i]);
        if (win[i])
            SDL_DestroyWindow(win[i]);
    }
    SDL_Quit();
    return 0;
}
