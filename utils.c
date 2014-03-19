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

#include <stdio.h>
#include <string.h>

#include "utils.h"

#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))

static uint32_t planes_used;
static uint32_t crtcs_used;
static uint32_t encoders_used;
static uint32_t connectors_used;

static const char *connector_type_str[] = {
	[DRM_MODE_CONNECTOR_Unknown]     = "Unknown",
	[DRM_MODE_CONNECTOR_VGA]         = "VGA",
	[DRM_MODE_CONNECTOR_DVII]        = "DVI-I",
	[DRM_MODE_CONNECTOR_DVID]        = "DVI-D",
	[DRM_MODE_CONNECTOR_DVIA]        = "DVI-A",
	[DRM_MODE_CONNECTOR_Composite]   = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO]      = "SVIDEO",
	[DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
	[DRM_MODE_CONNECTOR_Component]   = "Component",
	[DRM_MODE_CONNECTOR_9PinDIN]     = "DIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DP",
	[DRM_MODE_CONNECTOR_HDMIA]       = "HDMI-A",
	[DRM_MODE_CONNECTOR_HDMIB]       = "HDMI-B",
	[DRM_MODE_CONNECTOR_TV]          = "TV",
	[DRM_MODE_CONNECTOR_eDP]         = "eDP",
//	[DRM_MODE_CONNECTOR_VIRTUAL]     = "Virtual",
};

static const char *encoder_type_str[] = {
	[DRM_MODE_ENCODER_NONE]  = "None",
	[DRM_MODE_ENCODER_DAC]   = "DAC",
	[DRM_MODE_ENCODER_TMDS]  = "TMDS",
	[DRM_MODE_ENCODER_LVDS]  = "LVDS",
	[DRM_MODE_ENCODER_TVDAC] = "TVDAC",
};

void print_mode(const char *title, const drmModeModeInfo *mode)
{
	printf("%s [\n"
	       "\tname = %s\n"
	       "\tvrefresh = %u\n"
	       "\tclock = %u\n"
	       "\thdisplay = %u\n"
	       "\thsync_start = %u\n"
	       "\thsync_end = %u\n"
	       "\thtotal = %u\n"
	       "\tvdisplay = %u\n"
	       "\tvsync_start = %u\n"
	       "\tvsync_end = %u\n"
	       "\tvtotal = %u\n"
	       "\tflags = %x\n"
	       "]\n",
	       title,
	       mode->name,
	       mode->vrefresh,
	       mode->clock,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal,
	       mode->flags);
}

static int get_encoder_idx(drmModeResPtr res, drmModeEncoderPtr encoder)
{
	int i;

	for (i = 0; i < res->count_encoders; i++) {
		if (res->encoders[i] == encoder->encoder_id)
			return i;
	}

	return -1;
}

static int get_crtc_idx(drmModeResPtr res, drmModeCrtcPtr crtc)
{
	int i;

	for (i = 0; i < res->count_crtcs; i++) {
		if (res->crtcs[i] == crtc->crtc_id)
			return i;
	}

	return -1;
}

bool pick_connector(struct crtc *c, const char *name)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	int i;

	if (!res || !c || !name || c->connector_id)
		return false;

	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr connector;
		char connector_name[32];
		unsigned int type;

		if (connectors_used & (1 << i))
			continue;

		connector = drmModeGetConnector(fd, res->connectors[i]);
		if (!connector)
			continue;

		type = connector->connector_type;
		if (type >= ARRAY_SIZE(connector_type_str))
			type = 0;

		snprintf(connector_name, sizeof connector_name, "%s-%d",
			 connector_type_str[type],
			 connector->connector_type_id);

		if (!strcmp(name, connector_name)) {
			printf("picked connector [%u] id = %u, name = \"%s\"\n",
			       i, connector->connector_id, connector_name);
			c->connector_id = connector->connector_id;
			c->connector_idx = i;
			connectors_used = 1 << i;
		}

		drmModeFreeConnector(connector);

		if (c->connector_id)
			return true;
	}

	return false;
}

static bool encoder_has_free_crtc(drmModeResPtr res, drmModeEncoderPtr encoder)
{
	int i;

	for (i = 0; i < res->count_crtcs; i++) {
		if ((encoder->possible_crtcs & (1 << i)) && !(crtcs_used & (1 << i)))
			return true;
	}

	return false;
}

static bool reuse_old_encoder(int fd, drmModeResPtr res, struct crtc *c)
{
	drmModeConnectorPtr connector;
	drmModeEncoderPtr encoder;
	int encoder_idx;
	unsigned int type;

	connector = drmModeGetConnector(fd, c->connector_id);
	if (!connector)
		return false;

	if (!connector->encoder_id) {
		drmModeFreeConnector(connector);
		return false;
	}

	encoder = drmModeGetEncoder(fd, connector->encoder_id);
	if (!encoder) {
		drmModeFreeConnector(connector);
		return false;
	}

	drmModeFreeConnector(connector);

	encoder_idx = get_encoder_idx(res, encoder);

	if (encoders_used & (1 << encoder_idx)) {
		drmModeFreeEncoder(encoder);
		return false;
	}

	if (!encoder_has_free_crtc(res, encoder)) {
		drmModeFreeEncoder(encoder);
		return false;
	}

	type = encoder->encoder_type;
	if (type >= ARRAY_SIZE(encoder_type_str))
		type = 0;

	printf("picked encoder [%u] id = %u, type = \"%s\"\n",
	       encoder_idx, encoder->encoder_id, encoder_type_str[type]);

	c->encoder_id = encoder->encoder_id;
	c->encoder_idx = encoder_idx;
	encoders_used = 1 << encoder_idx;

	drmModeFreeEncoder(encoder);

	return true;
}

bool pick_encoder(struct crtc *c)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	drmModeConnectorPtr connector;
	int i;

	if (!c->connector_id)
		return false;

	if (reuse_old_encoder(fd, res, c))
		return true;

	connector = drmModeGetConnector(fd, c->connector_id);
	if (!connector)
		return false;

	for (i = 0; i < connector->count_encoders; i++) {
		drmModeEncoderPtr encoder;
		int encoder_idx;
		unsigned int type;

		encoder = drmModeGetEncoder(fd, connector->encoders[i]);
		if (!encoder)
			continue;

		encoder_idx = get_encoder_idx(res, encoder);

		if (encoders_used & (1 << encoder_idx)) {
			drmModeFreeEncoder(encoder);
			continue;
		}

		if (!encoder_has_free_crtc(res, encoder)) {
			drmModeFreeEncoder(encoder);
			continue;
		}

		type = encoder->encoder_type;
		if (type >= ARRAY_SIZE(encoder_type_str))
			type = 0;

		printf("picked encoder [%u] id = %u, type = \"%s\"\n",
		       encoder_idx, encoder->encoder_id, encoder_type_str[type]);

		c->encoder_id = encoder->encoder_id;
		c->encoder_idx = encoder_idx;
		encoders_used = 1 << encoder_idx;

		drmModeFreeEncoder(encoder);

		break;
	}

	drmModeFreeConnector(connector);

	return c->encoder_id != 0;
}

static bool reuse_old_crtc(int fd, drmModeResPtr res, struct crtc *c)
{
	drmModeEncoderPtr encoder;
	drmModeCrtcPtr crtc;
	int crtc_idx;

	encoder = drmModeGetEncoder(fd, c->encoder_id);
	if (!encoder)
		return false;

	if (!encoder->crtc_id) {
		drmModeFreeEncoder(encoder);
		return false;
	}

	crtc = drmModeGetCrtc(fd, encoder->crtc_id);
	if (!crtc) {
		drmModeFreeEncoder(encoder);
		return false;
	}

	drmModeFreeEncoder(encoder);

	crtc_idx = get_crtc_idx(res, crtc);

	if (crtcs_used & (1 << crtc_idx)) {
		drmModeFreeCrtc(crtc);
		return false;
	}

	printf("picked crtc [%u] id = %u\n", crtc_idx, crtc->crtc_id);

	c->crtc_id = crtc->crtc_id;
	c->crtc_idx = crtc_idx;
	crtcs_used = 1 << crtc_idx;

	drmModeFreeCrtc(crtc);

	return true;
}

bool pick_crtc(struct crtc *c)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	drmModeEncoderPtr encoder;
	int i;

	if (!c->encoder_id)
		return false;

	if (reuse_old_crtc(fd, res, c))
		return true;

	encoder = drmModeGetEncoder(fd, c->encoder_id);
	if (!encoder)
		return false;

	for (i = 0; i < res->count_crtcs; i++) {
		drmModeCrtcPtr crtc;

		crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc)
			continue;

		if (crtcs_used & (1 << i)) {
			drmModeFreeCrtc(crtc);
			continue;
		}

		if (!(encoder->possible_crtcs & (1 << i))) {
			drmModeFreeEncoder(encoder);
			continue;
		}

		printf("picked crtc [%u] id = %u\n", i, crtc->crtc_id);

		c->crtc_id = crtc->crtc_id;
		c->crtc_idx = i;
		crtcs_used |= 1 << i;

		drmModeFreeCrtc(crtc);

		break;
	}

	drmModeFreeEncoder(encoder);

	return c->crtc_id != 0;
}


enum drm_plane_type {
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_CURSOR,
};

static bool check_plane(int fd, drmModePlanePtr plane, int crtcid)
{
	drmModeObjectPropertiesPtr props;
	bool ok = false;
	uint32_t i;

	if ((crtcid >= 0) && !(plane->possible_crtcs & (1 << crtcid)))
		return false;

	props = drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props)
		return;

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop;

		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;

		printf("plane prop %s %u\n", prop->name, prop->prop_id);

		if (!strcmp(prop->name, "TYPE")) {
			uint32_t type = props->prop_values[i];
			if (crtcid >= 0)
				ok = (type == DRM_PLANE_TYPE_PRIMARY);
			else
				ok = (type == DRM_PLANE_TYPE_OVERLAY);
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);

	return ok;
}

static bool reuse_old_plane(int fd, drmModePlaneResPtr plane_res,
		struct plane *p, int crtcid)
{
	int i;

	for (i = 0; i < plane_res->count_planes; i++) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!plane)
			continue;

		if (!check_plane(fd, plane, crtcid)) {
			drmModeFreePlane(plane);
			continue;
		}

		if (plane->crtc_id != p->crtc->crtc_id) {
			drmModeFreePlane(plane);
			continue;
		}

		if (planes_used & (1 << i)) {
			drmModeFreePlane(plane);
			continue;
		}

		printf("picked plane [%u] id = %u\n", i, plane->plane_id);

		p->plane_id = plane->plane_id;
		p->plane_idx = i;
		planes_used = 1 << i;

		drmModeFreePlane(plane);

		return true;
	}

	return false;
}

bool pick_plane(struct plane *p, int crtcid)
{
	int fd = p->ctx->fd;
	drmModeResPtr res = p->ctx->res;
	drmModePlaneResPtr plane_res = p->ctx->plane_res;
	drmModeCrtcPtr crtc;
	int crtc_idx;
	int i;

	if (!p->crtc->crtc_id)
		return false;

	if (reuse_old_plane(fd, plane_res, p, crtcid))
		return true;

	crtc = drmModeGetCrtc(fd, p->crtc->crtc_id);
	if (!crtc)
		return false;

	crtc_idx = get_crtc_idx(res, crtc);

	for (i = 0; i < plane_res->count_planes; i++) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!plane)
			continue;

		if (!check_plane(fd, plane, crtcid)) {
			drmModeFreePlane(plane);
			continue;
		}

		if (!(plane->possible_crtcs & (1 << crtc_idx))) {
			drmModeFreePlane(plane);
			continue;
		}

		if (planes_used & (1 << i)) {
			drmModeFreePlane(plane);
			continue;
		}

		printf("picked plane [%u] id = %u\n", i, plane->plane_id);

		p->plane_id = plane->plane_id;
		p->plane_idx = i;
		planes_used = 1 << i;

		drmModeFreePlane(plane);

		break;
	}

	drmModeFreeCrtc(crtc);

	return p->plane_id != 0;
}

void release_connector(struct crtc *c)
{
	if (!c->connector_id)
		return;

	connectors_used &= ~(1 << c->connector_idx);
	c->connector_id = 0;
	c->connector_idx = 0;
}

void release_encoder(struct crtc *c)
{
	if (!c->encoder_id)
		return;

	encoders_used &= ~(1 << c->encoder_idx);
	c->encoder_id = 0;
	c->encoder_idx = 0;
}

void release_crtc(struct crtc *c)
{
	if (!c->crtc_id)
		return;

	crtcs_used &= ~(1 << c->crtc_idx);
	c->crtc_id = 0;
	c->crtc_idx = 0;
}

void release_plane(struct plane *p)
{
	if (!p->plane_id)
		return;

	planes_used &= ~(1 << p->plane_idx);
	p->plane_id = 0;
	p->plane_idx = 0;
}

bool init_ctx(struct ctx *ctx, int fd)
{
	drmModeResPtr res;
	drmModePlaneResPtr plane_res;

	res = drmModeGetResources(fd);
	if (!res)
		return false;

	plane_res = drmModeGetPlaneResources(fd);
        if (!plane_res) {
		drmModeFreeResources(res);
		return false;
	}

	ctx->fd = fd;
	ctx->res = res;
	ctx->plane_res = plane_res;

	return true;
}

void free_ctx(struct ctx *ctx)
{
	if (ctx->fd < 0)
		return;

	drmModeFreePlaneResources(ctx->plane_res);
	drmModeFreeResources(ctx->res);

	ctx->plane_res = NULL;
	ctx->res = NULL;
	ctx->fd = -1;
}

void init_crtc(struct crtc *c, struct ctx *ctx)
{
	memset(c, 0, sizeof *c);
	c->ctx = ctx;
}

void init_plane(struct plane *p, struct crtc *c, struct ctx *ctx)
{
	memset(p, 0, sizeof *p);
	p->crtc = c;
	p->ctx = ctx;
}

bool pick_mode(struct crtc *c, drmModeModeInfoPtr mode, const char *name)
{
	int fd = c->ctx->fd;
	drmModeConnectorPtr connector;
	int i;
	bool found = false;

	if (!c->connector_id)
		return false;

	connector = drmModeGetConnector(fd, c->connector_id);
	if (!connector)
		return false;

	for (i = 0; i < connector->count_modes; i++) {
		if (strcmp(connector->modes[i].name, name))
			continue;

		*mode = connector->modes[i];
		found = true;

		print_mode("picked mode", mode);

		break;
	}

	drmModeFreeConnector(connector);

	return found;
}
