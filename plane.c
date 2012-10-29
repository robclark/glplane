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
#include <errno.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>

#include <GL/gl.h>
#include <EGL/egl.h>

#include "utils.h"
#include "gutils.h"
#include "term.h"

//#define dprintf printf
#define dprintf(x...) do {} while (0)

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

#define PAGE_SIZE 4096

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

//#define IVB_VGA_HACK

static int fd;

enum plane_csc_matrix {
	PLANE_CSC_MATRIX_BT601,
	PLANE_CSC_MATRIX_BT709,
};

enum plane_csc_range {
	PLANE_CSC_RANGE_MPEG,
	PLANE_CSC_RANGE_JPEG,
};

struct region {
	int32_t x1;
	int32_t y1;
	int32_t x2;
	int32_t y2;
};

struct my_surface {
	struct surface base;
	int pending_events;
	EGLSurface egl_surface;
};

struct my_crtc {
	struct crtc base;

	int cur_buf;
	bool dirty;
	bool dirty_mode;

	unsigned int dispw;
	unsigned int disph;

	struct my_surface surf;

	uint32_t original_fb_id;
	uint32_t fb_id;

	drmModeModeInfo original_mode;
	drmModeModeInfo mode;

	unsigned int frames;
	struct timespec prev;

	struct {
		uint32_t src_x;
		uint32_t src_y;
		uint32_t fb;
		uint32_t mode;
		uint32_t connector_ids;
	} prop;

	uint32_t connector_ids[8];
};

struct my_plane {
	struct plane base;

	int cur_buf;
	bool dirty;

	enum plane_csc_matrix csc_matrix;
	enum plane_csc_range csc_range;

	struct my_surface surf;

	struct region src; /* 16.16 */
	struct region dst;

	bool enable;
	uint32_t fb_id;

	struct {
		uint32_t src_x;
		uint32_t src_y;
		uint32_t src_w;
		uint32_t src_h;

		uint32_t crtc_x;
		uint32_t crtc_y;
		uint32_t crtc_w;
		uint32_t crtc_h;

		uint32_t fb;
		uint32_t crtc;
	} prop;

	struct {
		float ang;
		float rad_dir;
		float rad;
		int w_dir;
		int w;
		int h_dir;
		int h;
	} state;
};

static bool throttle;

static int get_free_buffer(struct my_surface *surf)
{
	if (throttle && surf->pending_events > 0)
		return -1;
	if (surface_has_free_buffers(&surf->base))
		return 0;

	return -1;
}

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

static void plane_enable(struct my_plane *p, bool enable)
{
	if (p->enable == enable)
		return;
	p->enable = enable;
	p->dirty = true;
}

static void populate_crtc_props(int fd, struct my_crtc *c)
{
	drmModeObjectPropertiesPtr props;
	uint32_t i;

	props = drmModeObjectGetProperties(fd, c->base.crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props)
		return;

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop;

		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;

		printf("crtc prop %s\n", prop->name);

		if (!strcmp(prop->name, "SRC_X"))
			c->prop.src_x = prop->prop_id;
		else if (!strcmp(prop->name, "SRC_Y"))
			c->prop.src_y = prop->prop_id;
		else if (!strcmp(prop->name, "FB_ID"))
			c->prop.fb = prop->prop_id;
		else if (!strcmp(prop->name, "MODE"))
			c->prop.mode = prop->prop_id;
		else if (!strcmp(prop->name, "CONNECTOR_IDS"))
			c->prop.connector_ids = prop->prop_id;

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
}

static void populate_plane_props(int fd, struct my_plane *p)
{
	drmModeObjectPropertiesPtr props;
	uint32_t i;

	props = drmModeObjectGetProperties(fd, p->base.plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props)
		return;

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop;

		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;

		printf("plane prop %s\n", prop->name);

		if (!strcmp(prop->name, "SRC_X"))
			p->prop.src_x = prop->prop_id;
		else if (!strcmp(prop->name, "SRC_Y"))
			p->prop.src_y = prop->prop_id;
		else if (!strcmp(prop->name, "SRC_W"))
			p->prop.src_w = prop->prop_id;
		else if (!strcmp(prop->name, "SRC_H"))
			p->prop.src_h = prop->prop_id;
		else if (!strcmp(prop->name, "CRTC_X"))
			p->prop.crtc_x = prop->prop_id;
		else if (!strcmp(prop->name, "CRTC_Y"))
			p->prop.crtc_y = prop->prop_id;
		else if (!strcmp(prop->name, "CRTC_W"))
			p->prop.crtc_w = prop->prop_id;
		else if (!strcmp(prop->name, "CRTC_H"))
			p->prop.crtc_h = prop->prop_id;
		else if (!strcmp(prop->name, "FB_ID"))
			p->prop.fb = prop->prop_id;
		else if (!strcmp(prop->name, "CRTC_ID"))
			p->prop.crtc = prop->prop_id;

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
}

static void atomic_event(int fd, unsigned int seq, unsigned int tv_sec, unsigned int tv_usec,
			 uint32_t obj_id, uint32_t old_fb_id, void *user_data)
{
	struct my_plane *p = user_data;
	struct my_crtc *c = container_of(p->base.crtc, struct my_crtc, base);
	struct buffer *buf;
	int i;

	if (obj_id == p->base.plane_id)
		p->surf.pending_events--;
	else if (obj_id == c->base.crtc_id)
		c->surf.pending_events--;

	if (old_fb_id) {
		if (obj_id == p->base.plane_id) {
			buf = surface_find_buffer_by_fb_id(&p->surf.base, old_fb_id);
			if (buf)
				surface_buffer_put_fb(fd, &p->surf.base, buf);
		} else if (obj_id == c->base.crtc_id) {
			buf = surface_find_buffer_by_fb_id(&c->surf.base, old_fb_id);
			if (buf)
				surface_buffer_put_fb(fd, &c->surf.base, buf);
		} else
			printf("EVENT for unknown obj %u\n", obj_id);
	} else
		printf("EVENT w/o old_fb_id\n");
}


static void plane_commit(int fd, struct my_plane *p)
{
	drmModePropertySetPtr set;
	struct my_crtc *c = container_of(p->base.crtc, struct my_crtc, base);
	int r;
	uint32_t flags = DRM_MODE_ATOMIC_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	struct buffer *cbuf = NULL, *pbuf = NULL;

	if (!p->dirty && !c->dirty && !c->dirty_mode)
		return;

	set = drmModePropertySetAlloc();
	if (!set)
		return;

	if (c->dirty) {
		cbuf = surface_get_front(fd, &c->surf.base);
		if (!cbuf) {
			drmModePropertySetFree(set);
			return;
		}

		drmModePropertySetAdd(set,
				      c->base.crtc_id,
				      c->prop.fb,
				      cbuf->fb_id);

		drmModePropertySetAdd(set,
				      c->base.crtc_id,
				      c->prop.src_x,
				      0);

		drmModePropertySetAdd(set,
				      c->base.crtc_id,
				      c->prop.src_y,
				      0);
		c->surf.pending_events++;
	}

	if (c->dirty_mode) {
		drmModePropertySetAddBlob(set,
					  c->base.crtc_id,
					  c->prop.mode,
					  sizeof(c->mode),
					  &c->mode);

		c->connector_ids[0] = c->base.connector_id;
		drmModePropertySetAddBlob(set,
					  c->base.crtc_id,
					  c->prop.connector_ids,
					  4,
					  c->connector_ids);

		/* don't try nonblocking modeset, the kernel will reject it. */
		flags &= ~DRM_MODE_ATOMIC_NONBLOCK;
	}

	if (p->dirty) {
		if (p->enable) {
			pbuf = surface_get_front(fd, &p->surf.base);
			if (!pbuf) {
				if (cbuf) {
					surface_buffer_put_fb(fd, &c->surf.base, cbuf);
					c->surf.pending_events--;
				}
				drmModePropertySetFree(set);
				return;
			}
		}

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.fb,
				      pbuf ? pbuf->fb_id : 0);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.crtc,
				      p->base.crtc->crtc_id);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.src_x,
				      p->src.x1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.src_y,
				      p->src.y1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.src_w,
				      p->src.x2 - p->src.x1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.src_h,
				      p->src.y2 - p->src.y1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.crtc_x,
				      p->dst.x1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.crtc_y,
				      p->dst.y1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.crtc_w,
				      p->dst.x2 - p->dst.x1);

		drmModePropertySetAdd(set,
				      p->base.plane_id,
				      p->prop.crtc_h,
				      p->dst.y2 - p->dst.y1);
		if (pbuf)
			p->surf.pending_events++;
	}

	//r = drmModePropertySetCommit(fd, DRM_MODE_ATOMIC_TEST_ONLY, set);
	r = drmModePropertySetCommit(fd, flags, p, set);

	drmModePropertySetFree(set);

	if (r) {
		printf("setatomic returned %d:%s\n", errno, strerror(errno));

		if (p->dirty) {
			unsigned int src_w = p->src.x2 - p->src.x1;
			unsigned int src_h = p->src.y2 - p->src.y1;
			unsigned int dst_w = p->dst.x2 - p->dst.x1;
			unsigned int dst_h = p->dst.y2 - p->dst.y1;

			printf("plane = %u, crtc = %u, fb = %u\n",
			       p->base.plane_id, p->base.crtc->crtc_id, pbuf ? pbuf->fb_id : -1);

			printf("src = %u.%06ux%u.%06u+%u.%06u+%u.%06u\n",
			       src_w >> 16, ((src_w & 0xffff) * 15625) >> 10,
			       src_h >> 16, ((src_h & 0xffff) * 15625) >> 10,
			       p->src.x1 >> 16, ((p->src.x1 & 0xffff) * 15625) >> 10,
			       p->src.y1 >> 16, ((p->src.y1 & 0xffff) * 15625) >> 10);

			printf("dst = %ux%u+%d+%d\n",
			       dst_w, dst_h, p->dst.x1, p->dst.y1);
		}

		if (c->dirty)
			printf("crtc = %u, fb = %u\n", c->base.crtc_id, cbuf ? cbuf->fb_id : -1);

		if (c->dirty_mode) {
			print_mode("mode", &c->mode);

			printf("connector_id = %u\n", c->connector_ids[0]);
		}

		if (pbuf) {
			surface_buffer_put_fb(fd, &p->surf.base, pbuf);
			p->surf.pending_events--;
		}
		if (cbuf) {
			surface_buffer_put_fb(fd, &c->surf.base, cbuf);
			c->surf.pending_events--;
		}

		return;
	}

	if (0) {
		unsigned int src_w = p->src.x2 - p->src.x1;
		unsigned int src_h = p->src.y2 - p->src.y1;
		unsigned int dst_w = p->dst.x2 - p->dst.x1;
		unsigned int dst_h = p->dst.y2 - p->dst.y1;

		printf("setatomic ok\n");

		printf("plane = %u, crtc = %u, fb = %u\n",
		       p->base.plane_id, p->base.crtc->crtc_id, p->fb_id);

		printf("src = %u.%06ux%u.%06u+%u.%06u+%u.%06u\n",
		       src_w >> 16, ((src_w & 0xffff) * 15625) >> 10,
		       src_h >> 16, ((src_h & 0xffff) * 15625) >> 10,
		       p->src.x1 >> 16, ((p->src.x1 & 0xffff) * 15625) >> 10,
		       p->src.y1 >> 16, ((p->src.y1 & 0xffff) * 15625) >> 10);

		printf("dst = %ux%u+%d+%d\n",
		       dst_w, dst_h, p->dst.x1, p->dst.y1);
	}

	p->dirty = false;
	c->dirty = false;
	c->dirty_mode = false;
}

static void tp_sub(struct timespec *tp, const struct timespec *tp2)
{
	tp->tv_sec -= tp2->tv_sec;
	tp->tv_nsec -= tp2->tv_nsec;
	if (tp->tv_nsec < 0) {
		tp->tv_nsec += 1000000000L;
		tp->tv_sec--;
	}
}

static float adjust_angle(struct my_plane *p)
{
	const float ang_adj = M_PI / 500.0f;

	p->state.ang += ang_adj;
	if (p->state.ang > 2.0f * M_PI)
		p->state.ang -= 2.0f * M_PI;

	return p->state.ang;
}

static float adjust_radius(struct my_plane *p)
{
	struct my_crtc *c = container_of(p->base.crtc, struct my_crtc, base);
	float rad_max = sqrtf(c->dispw * c->dispw + c->disph * c->disph) / 2.0f;
	float rad_min = -rad_max;
	float rad_adj = rad_max / 500.0f;

	p->state.rad += rad_adj * p->state.rad_dir;
	if (p->state.rad > rad_max && p->state.rad_dir > 0.0f) {
		p->state.rad_dir = -p->state.rad_dir;
	} else if (p->state.rad < rad_min && p->state.rad_dir < 0.0f) {
		p->state.rad_dir = -p->state.rad_dir;
	}

	return p->state.rad;
}

static int adjust_w(struct my_plane *p)
{
	struct my_crtc *c = container_of(p->base.crtc, struct my_crtc, base);
	int w_max = c->dispw;
	int w_adj = 1;//c->dispw / 100;

	p->state.w += w_adj * p->state.w_dir;
	if (p->state.w > w_max && p->state.w_dir > 0) {
		p->state.w_dir = -p->state.w_dir;
	} else if (p->state.w < 0 && p->state.w_dir < 0) {
		p->state.w = 0;
		p->state.w_dir = -p->state.w_dir;
	}

	return p->state.w;
}

static int adjust_h(struct my_plane *p)
{
	struct my_crtc *c = container_of(p->base.crtc, struct my_crtc, base);
	int h_max = c->disph;
	int h_adj = 1;//c->disph / 100;

	p->state.h += h_adj * p->state.h_dir;
	if (p->state.h > h_max && p->state.h_dir > 0) {
		p->state.h_dir = -p->state.h_dir;
	} else if (p->state.h < 0 && p->state.h_dir < 0) {
		p->state.h = 0;
		p->state.h_dir = -p->state.h_dir;
	}

	return p->state.h;
}

static void render(EGLDisplay dpy, EGLContext ctx, struct my_surface *surf, bool col)
{
	static GLfloat view_rotx = 0.0, view_roty = 0.0, view_rotz = 0.0;
	static const GLfloat verts[3][2] = {
		{ -1, -1, },
		{  1, -1, },
		{  0,  1, },
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0, },
		{ 0, 1, 0, },
		{ 0, 0, 1, },
	};
	GLfloat ar = (GLfloat) surf->base.width / (GLfloat) surf->base.height;

	view_rotz += 1.0f;

	if (!eglMakeCurrent(dpy, surf->egl_surface, surf->egl_surface, ctx))
		return;

	glViewport(0, 0, (GLint) surf->base.width, (GLint) surf->base.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-ar, ar, -1, 1, 5.0, 60.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0, 0.0, -10.0);

	if (col)
		glClearColor(0.8, 0.4, 0.4, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.4, 0.4, 0.4, 0.0);

	glPushMatrix();
	glRotatef(view_rotx, 1, 0, 0);
	glRotatef(view_roty, 0, 1, 0);
	glRotatef(view_rotz, 0, 0, 1);

	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(3, GL_FLOAT, 0, colors);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	glPopMatrix();

	glFlush();

	eglSwapBuffers(dpy, surf->egl_surface);
}

static const EGLint attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 1,
	EGL_GREEN_SIZE, 1,
	EGL_BLUE_SIZE, 1,
	EGL_ALPHA_SIZE, 0,
	EGL_DEPTH_SIZE, 1,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
	EGL_NONE
};

static bool my_surface_alloc(struct my_surface *s,
			     int fd,
			     struct gbm_device *gbm,
			     unsigned int fmt,
			     unsigned int w,
			     unsigned int h,
			     EGLDisplay dpy)
{
	EGLint num_configs = 0;
	EGLConfig config;

	if (!surface_alloc(&s->base, fd, gbm, fmt, w, h))
		return false;

	if (!eglChooseConfig(dpy, attribs, &config, 1, &num_configs) || num_configs != 1) {
		memset(s, 0, sizeof *s);
		return false;
	}

	s->egl_surface = eglCreateWindowSurface(dpy, config, s->base.gbm_surface, NULL);
	if (s->egl_surface == EGL_NO_SURFACE) {
		surface_free(&s->base);
		return false;
	}

	return true;
}

static void handle_crtc(int fd,
			struct gbm_device *gbm,
			EGLDisplay dpy,
			EGLContext ctx,
			const char *mode_name,
			struct my_crtc *c, struct my_plane *p)
{
	if (!pick_mode(&c->base, &c->mode, mode_name))
		return;

	c->dirty_mode = true;

	if (0) {
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "1920x1080");
		c->mode.vrefresh = 60;
		c->mode.clock = 148500;
		c->mode.hdisplay = 1920;
		c->mode.hsync_start = 2008;
		c->mode.hsync_end = 2052;
		c->mode.htotal = 2200;
		c->mode.vdisplay = 1080;
		c->mode.vsync_start = 1084;
		c->mode.vsync_end = 1089;
		c->mode.vtotal = 1125;
		c->mode.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
#endif
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "1024x768");
		c->mode.vrefresh = 60;
		c->mode.clock = 65000;
		c->mode.hdisplay = 1024;
		c->mode.hsync_start = 1048;
		c->mode.hsync_end = 1184;
		c->mode.htotal = 1344;
		c->mode.vdisplay = 768;
		c->mode.vsync_start = 771;
		c->mode.vsync_end = 777;
		c->mode.vtotal = 806;
		c->mode.flags = 0xa;
#endif
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "1280x720");
		c->mode.vrefresh = 60;
		c->mode.clock = 74250;
		c->mode.hdisplay = 1280;
		c->mode.hsync_start = 1390;
		c->mode.hsync_end = 1430;
		c->mode.htotal = 1650;
		c->mode.vdisplay = 720;
		c->mode.vsync_start = 725;
		c->mode.vsync_end = 730;
		c->mode.vtotal = 750;
		c->mode.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
#endif
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "1366x768");
		c->mode.vrefresh = 60;
		c->mode.clock = 85885;
		c->mode.hdisplay = 1366;
		c->mode.hsync_start = 1439;
		c->mode.hsync_end = 1583;
		c->mode.htotal = 1800;
		c->mode.vdisplay = 768;
		c->mode.vsync_start = 769;
		c->mode.vsync_end = 772;
		c->mode.vtotal = 795;
		c->mode.flags = 6;
#endif
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "720x576");
		c->mode.vrefresh = 50;
		c->mode.clock = 27000;
		c->mode.hdisplay = 720;
		c->mode.hsync_start = 732;
		c->mode.hsync_end = 796;
		c->mode.htotal = 864;
		c->mode.vdisplay = 576;
		c->mode.vsync_start = 581;
		c->mode.vsync_end = 586;
		c->mode.vtotal = 625;
		c->mode.flags = 0xa;
#endif
#if 0
		snprintf(c->mode.name, sizeof c->mode.name, "1920x1080i");
		c->mode.vrefresh = 60;
		c->mode.clock = 74250;
		c->mode.hdisplay = 1920;
		c->mode.hsync_start = 2008;
		c->mode.hsync_end = 2052;
		c->mode.htotal = 2200;
		c->mode.vdisplay = 1080;
		c->mode.vsync_start = 1084;
		c->mode.vsync_end = 1094;
		c->mode.vtotal = 1125;
		c->mode.flags = 0x15;
#endif
		if (!pick_mode(&c->base, &c->mode, mode_name))
			return;

		c->dirty_mode = true;
	}
	printf("orig mode = %s, mode = %s\n", c->original_mode.name, c->mode.name);

	/* FIXME need to dig out the mode struct for c->mode_id instead */
	c->dispw = c->mode.hdisplay;
	c->disph = c->mode.vdisplay;

	if (!my_surface_alloc(&p->surf, fd, gbm, DRM_FORMAT_XRGB8888, 960, 576, dpy))
		return;

	p->src.x1 = 0 << 16;
	p->src.y1 = 0 << 16;
	p->src.x2 = p->surf.base.width << 16;
	p->src.y2 = p->surf.base.height << 16;

	p->dst.x1 = 0;
	p->dst.x2 = c->dispw/2;
	p->dst.y1 = 0;
	p->dst.y2 = c->disph/2;

	if (!my_surface_alloc(&c->surf, fd, gbm, DRM_FORMAT_XRGB8888, c->dispw, c->disph, dpy))
		return;

	p->cur_buf = -1;
	c->cur_buf = -1;

	render(dpy, ctx, &c->surf, false);
	render(dpy, ctx, &p->surf, true);

	c->dirty = true;
	p->dirty = true;

	plane_commit(fd, p);
}

static void animate_crtc(int fd,
			 EGLDisplay dpy,
			 EGLContext ctx,
			 struct my_crtc *c, struct my_plane *p)
{
	int i;

	if (get_free_buffer(&c->surf) < 0 || get_free_buffer(&p->surf) < 0)
		return;

	float rad = adjust_radius(p);
	float ang = adjust_angle(p);
	int w = adjust_w(p);
	int h = adjust_h(p);
	if (w < 4)
		w = 4;
	if (h < 4)
		h = 4;
	int x = rad * sinf(ang) + c->dispw / 2 - w/2;
	int y = rad * cosf(ang) + c->disph / 2 - h/2;

#if 0
	w = rand() % (c->dispw/2);
	h = rand() % (c->disph/2);
	if (w < 4)
		w = 4;
	if (h < 4)
		h = 4;
	x = rand() % (c->dispw-16) + 8 - w/2;
	y = rand() % (c->disph-16) + 8 - h/2;
#endif
#if 0
	w = c->dispw/3;
	h = c->disph;
	x = 2*c->dispw/3;
	y = 0;
#endif

	p->dst.x1 = x;
	p->dst.y1 = y;
	p->dst.x2 = p->dst.x1 + w;
	p->dst.y2 = p->dst.y1 + h;
	p->dirty = true;

	c->dirty = true;

	render(dpy, ctx, &c->surf, false);
	render(dpy, ctx, &p->surf, true);

	plane_commit(fd, p);

	c->frames++;

	if (c->frames >= 1000) {
		struct timespec cur;
		clock_gettime(CLOCK_MONOTONIC, &cur);
		struct timespec diff = cur;
		tp_sub(&diff, &c->prev);
		float secs = (float) diff.tv_sec + diff.tv_nsec / 1000000000.0f;
		printf("crtc [%d] id = %u: %u frames in %f secs, %f fps\n",
		       c->base.crtc_idx, c->base.crtc_id, c->frames, secs, c->frames / secs);
		c->prev = cur;
		c->frames = 0;
	}
}

int main(int argc, char *argv[])
{
	struct my_crtc c[8] = {
		[0] = {
		},
	};
	struct my_plane p[8] = {
		[0] = {
			.csc_matrix = PLANE_CSC_MATRIX_BT709,
			.csc_range = PLANE_CSC_RANGE_MPEG,
			.state = {
				.ang = 0.0f,
				.rad_dir = 1.0f,
				.rad = 0.0f,
				.w_dir = 1,
				.w = 0,
				.h_dir = 1,
				.h = 0,
			},
		},
	};
	struct ctx uctx = {};

	bool enable = true;
	bool quit = false;
	int r;
	int i;
	struct gbm_device *gbm;
	unsigned int handle;
	unsigned int crtc_idx;
	unsigned int connector_id = 0;
	unsigned int encoder_id = 0;
	drmEventContext evtctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.atomic_handler = atomic_event,
	};
	EGLDisplay dpy;
	EGLContext ctx;
	EGLint major, minor;
	EGLint num_configs = 0;
	EGLConfig config;
	int count_crtcs = 0;

	if (argc < 3)
		return 1;

	fd = drmOpen("i915", NULL);
	if (fd < 0)
		return 2;

	if (!init_ctx(&uctx, fd))
		return 3;

	for (i = 0; i < argc - 2; i++) {
		if (count_crtcs) {
			c[count_crtcs] = c[0];
			p[count_crtcs] = p[0];
		}

		init_crtc(&c[count_crtcs].base, &uctx);
		init_plane(&p[count_crtcs].base, &c[count_crtcs].base, &uctx);

		if (pick_connector(&c[count_crtcs].base, argv[i + 2]) &&
		    pick_encoder(&c[count_crtcs].base) &&
		    pick_crtc(&c[count_crtcs].base) &&
		    pick_plane(&p[count_crtcs].base)) {
			count_crtcs++;
			continue;
		}

		free_plane(&p[count_crtcs].base);
		free_crtc(&c[count_crtcs].base);
		free_encoder(&c[count_crtcs].base);
		free_connector(&c[count_crtcs].base);
	}

	gbm = gbm_create_device(fd);
	if (!gbm)
		return 5;

	dpy = eglGetDisplay(gbm);
	if (dpy == EGL_NO_DISPLAY)
		return 6;

	if (!eglInitialize(dpy, &major, &minor))
		return 7;

	eglBindAPI(EGL_OPENGL_API);

	if (!eglChooseConfig(dpy, attribs, &config, 1, &num_configs) || num_configs != 1)
		return 8;

	ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, NULL);
	if (!ctx)
		return 9;

	for (i = 0; i < count_crtcs; i++) {
		populate_crtc_props(fd, &c[i]);
		populate_plane_props(fd, &p[i]);
		plane_enable(&p[i], enable);
		handle_crtc(fd, gbm, dpy, ctx, argv[1], &c[i], &p[i]);
	}

	term_init();

	srand(time(NULL));

	bool test_running = false;
	struct timespec prev;

	while (!quit) {
		char cmd;
		fd_set fds;
		int maxfd;
		struct timeval timeout = { 0, 10 };

		FD_ZERO(&fds);

		maxfd = STDIN_FILENO;
		FD_SET(STDIN_FILENO, &fds);

		maxfd = max(maxfd, fd);
		FD_SET(fd, &fds);

		r = select(maxfd + 1, &fds, NULL, NULL, &timeout);

		if (r < 0 && errno == EINTR)
			continue;

		if (FD_ISSET(fd, &fds))
			drmHandleEvent(fd, &evtctx);

		if (!FD_ISSET(STDIN_FILENO, &fds) && test_running) {
			for (i = 0; i < count_crtcs; i++)
				animate_crtc(fd, dpy, ctx, &c[i], &p[i]);
		}

		if (!FD_ISSET(STDIN_FILENO, &fds))
			continue;

		if (read(0, &cmd, 1) < 0)
			break;

		switch (cmd) {
		case 'o':
			for (i = 0; i < count_crtcs; i++) {
				enable = !enable;
				plane_enable(&p[i], enable);
				plane_commit(fd, &p[i]);
			}
			break;
		case 'q':
			quit = 1;
			break;
		case 's':
		case 'x':
			for (i = 0; i < count_crtcs; i++) {
				p[i].dst.y1 += (cmd == 's') ? -1 : 1;
				p[i].dst.y2 += (cmd == 's') ? -1 : 1;
				p[i].dirty = true;
				c[i].dirty = true;
				render(dpy, ctx, &c[i].surf, false);
				render(dpy, ctx, &p[i].surf, true);
				plane_commit(fd, &p[i]);
			}
			break;
		case 'S':
		case 'X':
			for (i = 0; i < count_crtcs; i++) {
				p[i].dst.y2 += (cmd == 'S') ? -1 : 1;
				p[i].dirty = true;
				c[i].dirty = true;
				render(dpy, ctx, &c[i].surf, false);
				render(dpy, ctx, &p[i].surf, true);
				plane_commit(fd, &p[i]);
			}
			break;
		case 'z':
		case 'c':
			for (i = 0; i < count_crtcs; i++) {
				p[i].dst.x1 += (cmd == 'z') ? -1 : 1;
				p[i].dst.x2 += (cmd == 'z') ? -1 : 1;
				p[i].dirty = true;
				c[i].dirty = true;
				render(dpy, ctx, &c[i].surf, false);
				render(dpy, ctx, &p[i].surf, true);
				plane_commit(fd, &p[i]);
			}
			break;
		case 'Z':
		case 'C':
			for (i = 0; i < count_crtcs; i++) {
				p[i].dst.x2 += (cmd == 'Z') ? -1 : 1;
				p[i].dirty = true;
				c[i].dirty = true;
				render(dpy, ctx, &c[i].surf, false);
				render(dpy, ctx, &p[i].surf, true);
				plane_commit(fd, &p[i]);
			}
			break;
		case 't':
			test_running = !test_running;
			if (test_running) {
				clock_gettime(CLOCK_MONOTONIC, &prev);
				for (i = 0; i < count_crtcs; i++) {
					c[i].prev = prev;
					c[i].frames = 0;
				}
			}
			break;
		case 'T':
			throttle = !throttle;
			break;
		}
	}

	term_deinit();

	for (i = 0; i < count_crtcs; i++) {
		c[i].dirty = true;
		c[i].mode = c[i].original_mode;
		c[i].dirty_mode = true;

		plane_enable(&p[i], false);
		plane_commit(fd, &p[i]);

		surface_free(&c[i].surf.base);
		surface_free(&p[i].surf.base);
	}

	gbm_device_destroy(gbm);

	free_ctx(&uctx);

	drmClose(fd);

	return 0;
}
