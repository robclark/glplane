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

#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <stddef.h>

#define container_of(ptr, type, member) ({			\
	const typeof(((type *)NULL)->member) *__ptr = (ptr);	\
	(type*)((char*)__ptr - offsetof(type, member));		\
})

struct ctx {
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr plane_res;
};

struct crtc {
	struct ctx *ctx;

	uint32_t crtc_id;
	uint32_t crtc_idx;
	uint32_t encoder_id;
	uint32_t encoder_idx;
	uint32_t connector_id;
	uint32_t connector_idx;
};

struct plane {
	struct ctx *ctx;

	struct crtc *crtc;

	uint32_t plane_id;
	uint32_t plane_idx;
};

bool init_ctx(struct ctx *ctx, int fd);
void free_ctx(struct ctx *ctx);

void init_crtc(struct crtc *c, struct ctx *ctx);
void init_plane(struct plane *p, struct crtc *c, struct ctx *ctx);

bool pick_connector(struct crtc *c, const char *name);
bool pick_encoder(struct crtc *c);
bool pick_crtc(struct crtc *c);
bool pick_plane(struct plane *p);

void release_connector(struct crtc *c);
void release_encoder(struct crtc *c);
void release_crtc(struct crtc *c);
void release_plane(struct plane *p);

void print_mode(const char *title, const drmModeModeInfo *mode);
bool pick_mode(struct crtc *c, drmModeModeInfoPtr mode, const char *name);

#endif
