#ifndef GL_H
#define GL_H

#include "common.h"

bool gl_init(void);
void gl_fini(void);

bool gl_surf_init(EGLDisplay dpy, EGLConfig config, struct my_surface *s);
void gl_surf_fini(EGLDisplay dpy, struct my_surface *s);

void gl_surf_render(EGLDisplay dpy, EGLContext ctx,
		    struct my_surface *surf,
		    bool col, bool anim, bool blur);

#endif
