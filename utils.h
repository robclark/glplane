/*
 *
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
