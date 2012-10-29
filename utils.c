/*
 *
 */

#include "utils.h"

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
	[DRM_MODE_CONNECTOR_VIRTUAL]     = "Virtual",
};

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

void pick_connector(struct crtc *c, const char *name)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	int i;

	if (!res || !c || !name || c->connector_id)
		return;

	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr connector;
		char connector_name[32];

		if (connectors_used & (1 << i))
			continue;

		connector = drmModeGetConnector(fd, res->connectors[i]);
		if (!connector)
			continue;

		snprintf(connector_name, sizeof connector_name, "%s-%d\n",
			 connector_type_str[connector->connector_type],
			 connector->connector_type_id);

		if (!strcmp(name, connector_name)) {
			printf("picked connector [%u] = id = %u, name = \"%s\"\n",
			       connector->connector_id, connector_name);
			c->connector_id = connector->connector_id;
			c->connector_idx = i;
			connecors_used = 1 << i;
		}

		drmModeFreeConnector(connector);

		if (c->connector_id)
			return;
	}
}

static bool encoder_has_free_crtc(drmModeResPtr res, drmModeEncoderPtr encoder)
{
	int i;

	for (i = 0; i < res->count_crtcs; i++) {
		if ((encoder->possible_crtcs & (1 << i)) && !(used_crtcs & (1 << i)))
			return true;
	}

	return false;
}

static void reuse_old_encoder(int fd, drmModeResPtr res, struct crtc *c)
{
	struct drmModeConnectorPtr connector;
	struct drmModeEncoderPtr encoder;
	int encoder_idx;

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

	c->encoder_id = encoder->encoder_id;
	c->encoder_idx = encoder_idx;
	encoders_used = 1 << encoder_idx;

	drmModeFreeEncoder(encoder);

	return true;
}

void pick_encoder(struct crtc *c)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	drmModeConnectorPtr connector;
	int i;

	if (!c->connector_id)
		return;

	if (reuse_old_encoder(fd, res, c))
		return;

	connector = drmModeGetConnector(fd, c->connector_id);
	if (!connector)
		return;

	for (i = 0; i < connector->count_encoders; i++) {
		drmModeEncoderPtr encoder;
		int encoder_idx;

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

		c->encoder_id = encoder->encoder_id;
		c->encoder_idx = encoder_idx;
		encoders_used = 1 << encoder_idx;

		drmModeFreeEncoder(encoder);

		break;
	}

	drmModeFreeConnector(connector);
}

static bool reuse_old_crtc(int fd, drmModeResPtr res, struct crtc *c)
{
	struct drmModeEncoderPtr encoder;
	struct drmModeCrtcPtr crtc;
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

	c->crtc_id = crtc->crtc_id;
	c->crtc_idx = crtc_idx;
	crtcs_used = 1 << crtc_idx;

	drmModeFreeCrtc(crtc);

	return true;
}

void pick_crtc(struct crtc *c)
{
	int fd = c->ctx->fd;
	drmModeResPtr res = c->ctx->res;
	drmModeEncoderPtr encoder;
	int i;

	if (!c->encoder_id)
		return;

	if (reuse_old_crtc(fd, res, c))
		return;

	encoder = drmModeGetEncoder(fd, c->encoder_id);
	if (!encoder)
		return;

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

		c->crtc_id = crtc->crtc_id;
		c->crtc_idx = i;
		crtcs_used |= 1 << i;

		drmModeFreeCrtc(crtc);

		break;
	}

	drmModeFreeEncoder(encoder);
}

static bool reuse_old_plane(int fd, drmModePlaneResPtr plane_res, struct plane *p)
{
	if (i = 0; i < plane_res->count_planes; i++) {
		struct drmModePlanePtr plane;

		plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!plane)
			continue;

		if (plane->crtc_id != p->c->crtc_id) {
			drmModeFreePlane(plane);
			continue;
		}

		if (planes_used & (1 << i)) {
			drmModeFreePlane(plane);
			continue;
		}

		p->plane_id = plane->plane_id;
		p->plane_idx = i;
		planes_used = 1 << i;

		drmModeFreePlane(plane);

		return true;
	}

	return false;
}

void pick_plane(struct plane *p)
{
	int fd = p->ctx->fd;
	drmModeResPtr res = p->ctx->res;
	drmModePlaneResPtr plane_res = p->ctx->plane_res;
	int i;

	if (!p->c->crtc_id)
		return;

	if (reuse_old_plane(fd, plane_res, p))
		return;

	crtc = drmModeGetCrtc(fd, p->c->crtc_id);
	if (!crtc)
		return;

	crtc_idx = get_crtc_idx(res, crtc);

	for (i = 0; i < plane_res->count_planes; i++) {
		drmModePlanePtr plane;
		int j;

		plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!plane)
			continue;

		if (!(plane->possible_crtcs & (1 << crtc_idx))) {
			drmModeFreePlane(plane);
			continue;
		}

		if (planes_used & (1 << i)) {
			drmModeFreePlane(plane);
			continue;
		}

		p->plane_id = plane->plane_id;
		p->plane_idx = i;
		planes_used = 1 << i;

		drmModeFreePlane(plane);

		break;
	}

	drmModeFreeCrtc(crtc);
}

void free_connector(struct crtc *c)
{
	if (!c->connector_id)
		return;

	connectors_used &= ~(1 << c->connector_idx);
	c->connector_id = 0;
	c->connector_idx = 0;
}

void free_encoder(struct crtc *c)
{
	if (!c->encoder_id)
		return;

	encoders_used &= ~(1 << c->encoder_idx);
	c->encoder_id = 0;
	c->encoder_idx = 0;
}

void free_crtc(struct crtc *c)
{
	if (!c->crtc_id)
		return;

	crtcs_used &= ~(1 << c->crtc_idx);
	c->crtc_id = 0;
	c->crtc_idx = 0;
}

void free_plane(struct plane *p)
{
	if (!p->plane_id)
		return;

	planes_used &= ~(1 << p->plane_idx);
	p->plane_id = 0;
	p->plane_idx = 0;
}

bool init_ctx(struct ctx *ctx)
{
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr plane_res;

	fd = = drmOpen("i915", NULL);
	if (fd < 0)
		return false;

	res = drmModeGetResources(fd);
	if (!res) {
		close(fd);
		return false;
	}

	plane_res = drmModeGetPlaneResources(fd);
        if (!plane_res) {
		drmModeFreeResources(res);
		close(fd);
		return false;
	}

	return true;
}

void free_ctx(struct ctx *ctx)
{
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr plane_res;

	fd = = drmOpen("i915", NULL);
	if (fd < 0)
		return false;

	res = drmModeGetResources(fd);
	if (!res) {
		close(fd);
		return false;
	}

	plane_res = drmModeGetPlaneResources(fd);
        if (!plane_res) {
		drmModeFreeResources(res);
		close(fd);
		return false;
	}

	return true;
}


