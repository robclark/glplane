/*
 *
 */

#include "gutils.h"

int surface_get_free_buffer(struct base_surface *s)
{
	if (gbm_surface_has_free_buffers(s->gbm_surface))
		return 0;
	return -1;
}

struct base_buffer *surface_find_buffer_by_fb_id(struct base_surface *s, uint32_t fb_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s->buffers); i++) {
		if (s->buffers[i].fb_id == fb_id) {
			assert(s->buffers[i].ref >= 0);
			return &s->buffers[i];
		}
	}

	return NULL;
}

void surface_buffer_put_fb(int fd, struct base_surface *s, struct base_buffer *b)
{
	b->ref--;
	gbm_surface_release_buffer(s->gbm_surface, b->bo);
}

static void buffer_nuke(struct gbm_bo *bo, void *data)
{
	struct base_buffer *b = data;

	assert(b->ref == 0);

	drmModeRmFB(b->fd, b->fb_id);
}

struct base_buffer *surface_get_front(int fd, struct base_surface *s)
{
	struct gbm_bo *bo;
	struct base_buffer *b;
	int i;

	bo = gbm_surface_lock_front_buffer(s->gbm_surface);
	if (!bo)
		return NULL;

	b = gbm_bo_get_user_data(bo);
	if (b) {
		assert(buf->ref >= 0);
		assert(buf->bo == bo);
		b->ref++;
		return b;
	}

	for (i = 0; i < ARRAY_SIZE(s->buffers); i++) {
		if (s->buffers[i].ref == 0) {
			b = &s->buffers[i];
			break;
		}
	}

	if (!b) {
		gbm_surface_release_buffer(s->gbm_surface, bo);
		return NULL;
	}

	memset(b, 0, sizeof *b);

	b->fd = fd;
	b->ref = 1;
	b->bo = bo;
	b->handle[0] = gbm_bo_get_handle(bo).u32;
	b->stride[0] = gbm_bo_get_stride(bo);
	b->size = b->stride[0] * s->height;

	if (drmModeAddFB2(fd, s->width, s->height, s->fmt, b->handle, b->stride, b->offset, &b->fb_id, 0)) {
		gbm_surface_release_buffer(s->gbm_surface, bo);
		b->ref = 0;
		return NULL;
	}

	gbm_bo_set_user_data(bo, b, buffer_nuke);

	return b;
}

void surface_free(struct base_surface *s)
{
	if (!s->gbm_surface)
		return;

	gbm_surface_destroy(s->gbm_surface);
	memset(s, 0, sizeof *s);
}

bool surface_alloc(int fd,
		   EGLDisplay *dpy,
		   struct gbm_device *gbm,
		   struct base_surface *s,
		   unsigned int fmt,
		   unsigned int width,
		   unsigned int height)
{
	EGLint num_configs = 0;
	uint32_t gbm_fmt;
	EGLConfig config;

	switch (fmt) {
	case DRM_FORMAT_XRGB8888:
		gbm_fmt = GBM_FORMAT_XRGB8888;
		break;
	default:
		return 1;
	}

	memset(s, 0, sizeof *s);

	s->fmt = fmt;
	s->width = width;
	s->height = height;

	if (!eglChooseConfig(dpy, attribs, &config, 1, &num_configs) || num_configs != 1) {
		memset(s, 0, sizeof *s);
		return false;
	}

	s->gbm_surface = gbm_surface_create(gbm, width, height, gbm_fmt, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!s->gbm_surface) {
		memset(s, 0, sizeof *s);
		return false;
	}

	s->egl_surface = eglCreateWindowSurface(dpy, config, s->gbm_surface, NULL);
	if (s->egl_surface == EGL_NO_SURFACE) {
		gbm_surface_destroy(s->gbm_surface);
		memset(s, 0, sizeof *s);
		return false;
	}

	return true;
}
