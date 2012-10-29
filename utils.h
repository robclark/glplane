/*
 *
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct ctx {
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr plane_res;
};

struct base_crtc {
	struct ctx *ctx;

	uint32_t crtc_id;
	uint32_t crtc_idx;
	uint32_t encoder_id;
	uint32_t connector_id;
};

struct base_plane {
	struct ctx *ctx;

	uint32_t plane_id;
	struct crtc *c;
};

bool init_ctx(struct ctx *ctx, int fd);
void free_ctx(struct ctx *ctx);

void init_crtc(struct base_crtc *c, struct ctx *ctx);
void init_plane(struct base_plane *p, struct base_crtc *c, struct ctx *ctx);

void pick_connector(struct base_crtc *c, const char *name);
void pick_encoder(struct base_crtc *c);
void pick_crtc(struct base_crtc *c);
void pick_plane(struct base_plane *p);

void release_connector(struct base_crtc *c);
void release_encoder(struct base_crtc *c);
void release_crtc(struct base_crtc *c);
void release_plane(struct base_plane *p);

#endif
