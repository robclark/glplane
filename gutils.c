
struct buffer *find_buffer_by_fb_id(struct surface *s, uint32_t fb_id)
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

void buffer_put_fb(int fd, struct surface *s, struct buffer *b)
{
	b->ref--;
	gbm_surface_release_buffer(s->gs, b->bo);
}

void buffer_nuke(struct gbm_bo *bo, void *data)
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

	bo = gbm_surface_lock_front_buffer(s->gs);
	if (!bo)
		return NULL;

	b = gbm_bo_get_user_data(bo);
	if (b) {
		assert(buf->ref >= 0);
		assert(buf->bo == bo);
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
		gbm_surface_release_buffer(s->gs, bo);
		return NULL;
	}

	memset(b, 0, sizeof *b);

	b->fd = fd;
	b->ref = 1;
	b->bo = bo;
	b->handle[0] = gbm_bo_get_handle(b->bo).u32;
	b->stride[0] = gbm_bo_get_stride(b->bo);
	b->size = b->stride[0] * s->height;

	if (drmModeAddFB2(fd, s->width, s->height, s->fmt, b->handle, b->stride, b->offset, &b->fb_id, 0)) {
		gbm_surface_release_buffer(s->gs, bo);
		b->ref = 0;
		return NULL;
	}

	gbm_bo_set_user_data(bo, b, buffer_nuke);

	return b;
}

