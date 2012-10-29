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

struct crtc {
	struct ctx *ctx;

	uint32_t crtc_id;
	uint32_t crtc_idx;
	uint32_t encoder_id;
	uint32_t connector_id;
};

struct plane {
	struct ctx *ctx;

	uint32_t plane_id;
	struct crtc *c;
};

bool init_ctx(struct ctx *ctx);
void free_ctx(struct ctx *ctx);

void pick_connector(drmModeResPtr res, struct crtc *c, const char *name);
void pick_encoder(drmModeResPtr res, struct crtc *c);
void pick_crtc(drmModeResPtr res, struct crtc *c);
void pick_plane(drmModeResPtr res, drmModePlaneResPtr plane_res, struct plane *p);

void free_connector(struct crtc *c);
void free_encoder(struct crtc *c);
void free_crtc(struct crtc *c);
void free_plane(struct plane *p);

#endif
