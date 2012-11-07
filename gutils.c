/*
 * Copyright (C) 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <gbm.h>
#include <drm_fourcc.h>

#include <xf86drmMode.h>

#include "gutils.h"

#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))

bool surface_has_free_buffers(struct surface *s)
{
	return gbm_surface_has_free_buffers(s->gbm_surface);
}

struct buffer *surface_find_buffer_by_fb_id(struct surface *s, uint32_t fb_id)
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

void surface_buffer_put_fb(struct surface *s, struct buffer *b)
{
	b->ref--;
	gbm_surface_release_buffer(s->gbm_surface, b->bo);
}

static void buffer_nuke(struct gbm_bo *bo, void *data)
{
	struct buffer *b = data;

	assert(b->ref == 0);

	drmModeRmFB(b->fd, b->fb_id);
}

struct buffer *surface_get_front(int fd, struct surface *s)
{
	struct gbm_bo *bo;
	struct buffer *b;
	int i;

	bo = gbm_surface_lock_front_buffer(s->gbm_surface);
	if (!bo)
		return NULL;

	b = gbm_bo_get_user_data(bo);
	if (b) {
		assert(b->ref >= 0);
		assert(b->bo == bo);
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

void surface_free(struct surface *s)
{
	if (!s->gbm_surface)
		return;

	gbm_surface_destroy(s->gbm_surface);
	memset(s, 0, sizeof *s);
}

bool surface_alloc(struct surface *s,
		   struct gbm_device *gbm,
		   unsigned int fmt,
		   unsigned int width,
		   unsigned int height)
{
	uint32_t gbm_fmt;

	switch (fmt) {
	case DRM_FORMAT_XRGB8888:
		gbm_fmt = GBM_FORMAT_XRGB8888;
		break;
	default:
		return false;
	}

	memset(s, 0, sizeof *s);

	s->fmt = fmt;
	s->width = width;
	s->height = height;

	s->gbm_surface = gbm_surface_create(gbm, width, height, gbm_fmt, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!s->gbm_surface) {
		memset(s, 0, sizeof *s);
		return false;
	}

	return true;
}
