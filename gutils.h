/*
 *
 */

#ifndef GUTILS_H
#define GUTILS_H

#include <stdbool.h>
#include <stdint.h>

struct buffer {
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

struct surface {
	struct gbm_surface *gbm_surface;
	struct buffer buffers[8];
	unsigned int width;
	unsigned int height;
	uint32_t fmt;
};

struct gbm_device;
struct gbm_surface;
struct gbm_bo;

bool surface_has_free_buffers(struct surface *s);

struct buffer *surface_find_buffer_by_fb_id(struct surface *s, uint32_t fb_id);

void surface_buffer_put_fb(int fd, struct surface *s, struct buffer *b);

struct buffer *surface_get_front(int fd, struct surface *s);

void surface_free(struct surface *s);

bool surface_alloc(struct surface *s,
		   int fd,
		   struct gbm_device *gbm,
		   unsigned int fmt,
		   unsigned int width,
		   unsigned int height);

#endif
