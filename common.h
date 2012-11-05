#ifndef COMMON_H
#define COMMON_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "gutils.h"

struct my_surface {
	struct surface base;
	int pending_events;
	EGLSurface egl_surface;
	GLuint fbo[2];
	GLuint tex[2];
	GLfloat rot, phase;
};

#endif
