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

void surface_buffer_put_fb(struct surface *s, struct buffer *b);

struct buffer *surface_get_front(int fd, struct surface *s);

void surface_free(struct surface *s);

bool surface_alloc(struct surface *s,
		   struct gbm_device *gbm,
		   unsigned int fmt,
		   unsigned int width,
		   unsigned int height);

#endif
