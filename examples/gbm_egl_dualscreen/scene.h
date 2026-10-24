/* The GLES2 drawing for the GBM/EGL example, kept apart from the DRM/EGL setup so it can be
 * tested headless (see test_scene.c). */
#ifndef SCENE_H
#define SCENE_H

/* Compile shaders and create GL objects in the current context. 0 on success. */
int scene_init(void);
/* Draw one frame for panel `panel` (0 top, 1 bottom) at time `t` seconds into the whole surface. */
void scene_draw(int panel, int width, int height, float t);
void scene_fini(void);

#endif
