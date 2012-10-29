/*
 *
 */

#ifndef GUTILS_H
#define GUTILS_H

struct base_buffer {
	int fd;
	unsigned int size;
	uint32_t offset[4];
	uint32_t stride[4];
	uint32_t handle[4];
	unsigned int width;
	unsigned int height;
	uint32_t fmt;
	uint32_t fb_id;
	int ref;
	struct gbm_bo *bo;
};

struct base_surface {
	struct gbm_surface *gbm_surface;
	struct base_buffer buffers[8];
	unsigned int width;
	unsigned int height;
	uint32_t fmt;
	EGLSurface surface;
};

bool surface_has_free_buffers(struct base_surface *s);

struct base_buffer *surface_find_buffer_by_fb_id(struct base_surface *s, uint32_t fb_id);

void surface_buffer_put_fb(int fd, struct base_surface *s, struct base_buffer *b);

struct base_buffer *surface_get_front(int fd, struct base_surface *s);

void surface_free(struct base_surface *s);

bool surface_alloc(int fd,
		   EGLDisplay *dpy,
		   struct gbm_device *gbm,
		   struct base_surface *s,
		   unsigned int fmt,
		   unsigned int width,
		   unsigned int height);

#endif
