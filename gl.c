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
#include <math.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "gl.h"

static GLuint normal_program;
static GLuint ripple_program;
static GLuint blur_program;

static GLint create_program(const char *vert_source, const char *frag_source)
{
	GLint log_length;
	char log[1024];
	GLint status;

	GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
	if (!vert_shader) {
		return 0;
	}
	glShaderSource(vert_shader, 1, &vert_source, NULL);
	glCompileShader(vert_shader);
	status = 0;
	glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		log[0] = '?';
		log[1] = '\0';
		glGetShaderInfoLog(vert_shader, sizeof(log), &log_length, log);
		printf("Vertex shader compilation failed:\n%s\n", log);
		glDeleteShader(vert_shader);
		return 0;
	}

	GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!vert_shader) {
		glDeleteShader(vert_shader);
		return 0;
	}
	glShaderSource(frag_shader, 1, &frag_source, NULL);
	glCompileShader(frag_shader);
	status = 0;
	glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		log[0] = '?';
		log[1] = '\0';
		glGetShaderInfoLog(frag_shader, sizeof(log), &log_length, log);
		printf("Fragment shader compilation failed:\n%s\n", log);
		glDeleteShader(frag_shader);
		glDeleteShader(vert_shader);
		return 0;
	}

	GLuint program = glCreateProgram();
	if (!program) {
		glDeleteShader(frag_shader);
		glDeleteShader(vert_shader);
		return 0;
	}
	glAttachShader(program, vert_shader);
	glAttachShader(program, frag_shader);
	glDeleteShader(frag_shader);
	glDeleteShader(vert_shader);
	glLinkProgram(program);
	status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		log[0] = '?';
		log[1] = '\0';
		glGetProgramInfoLog(program, sizeof(log), &log_length, log);
		printf("Program linking failed:\n%s\n", log);
		glDeleteProgram(program);
		return 0;
	}

	return program;
}

static GLuint create_normal_program(void)
{
	static const char *vert_source =
		"attribute vec2 in_position;\n"
		"attribute vec4 in_color;\n"
		"uniform float un_rot;\n"
		"uniform vec2 un_scale;\n"
		"varying vec4 color;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   mat2 mat = mat2(cos(un_rot), sin(un_rot), -sin(un_rot), cos(un_rot));\n"
		"   gl_Position = vec4(mat * in_position * un_scale, 0.0f, 1.0f);\n"
		"   color = in_color;\n"
		"}\n";
	static const char *frag_source =
		"varying vec4 color;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   gl_FragColor = color;\n"
		"}\n";

	return create_program(vert_source, frag_source);
}

static GLuint create_ripple_program(void)
{
	static const char *vert_source =
		"attribute vec2 in_position;\n"
		"attribute vec2 in_tex;\n"
		"uniform float un_phase;\n"
		"varying vec2 texcoord;\n"
		"varying float pos;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(in_position, 0.0f, 1.0f);\n"
		"   pos = in_position.y * 25.0f + un_phase;\n"
		"   texcoord = in_tex;\n"
		"}\n";
	static const char *frag_source =
		"uniform sampler2D tex;\n"
		"varying vec2 texcoord;\n"
		"varying float pos;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   float x = texcoord.x + 0.05f * sin(pos);\n"
		"   gl_FragColor = texture2D(tex, vec2(x, texcoord.y));\n"
		"}\n";

	return create_program(vert_source, frag_source);
}

static GLuint create_blur_program(void)
{
	static const char *vert_source =
		"attribute vec2 in_position;\n"
		"attribute vec2 in_tex;\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(in_position, 0.0f, 1.0f);\n"
		"   texcoord = in_tex;\n"
		"}\n";
	static const char *frag_source =
		"uniform sampler2D tex;\n"
		"uniform vec2 texdisp[5];\n"
		"uniform float coefs[5];\n"
		"varying vec2 texcoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		"   vec4 t[5];\n"
		"   int i;\n"
		"   for (i = 0; i < 5; i++)\n"
		"      t[i] = texture2D(tex, vec2(texcoord.x + texdisp[i].x, texcoord.y + texdisp[i].y)) * coefs[i];\n"
		"   vec4 tt = vec4(0.0f);\n"
		"   for (i = 0; i < 5; i++)\n"
		"      tt += t[i];\n"
		"   gl_FragColor = tt;\n"
		"}\n";

	return create_program(vert_source, frag_source);
}

#define min(a,b) ((a) < (b) ? (a) : (b))

static void render(GLfloat rot)
{
	GLint viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, viewport);
	GLfloat w;
	GLfloat h;
	const GLfloat tri_size = 0.75f;
	if (viewport[2] > viewport[3]) {
		w = tri_size * viewport[3] / viewport[2];
		h = tri_size;
	} else {
		h = tri_size * viewport[2] / viewport[3];
		w = tri_size;
	}
	const GLfloat verts[] = {
		 0.0f,                1.0f,
		 0.5f * sqrtf(3.0f), -0.5f,
		-0.5f * sqrtf(3.0f), -0.5f,
	};
	const GLfloat colors[] = {
		1.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
	};

	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);

	GLint position_attr = glGetAttribLocation(program, "in_position");
	GLint color_attr = glGetAttribLocation(program, "in_color");

	glVertexAttribPointer(position_attr, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(color_attr, 4, GL_FLOAT, GL_FALSE, 0, colors);

	glEnableVertexAttribArray(position_attr);
	glEnableVertexAttribArray(color_attr);

	GLint rot_unif = glGetUniformLocation(program, "un_rot");
	GLint scale_unif = glGetUniformLocation(program, "un_scale");

	glUniform1f(rot_unif, rot);
	glUniform2f(scale_unif, w, h);

	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void render_ripple(GLfloat phase)
{
	const GLfloat verts[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		 1.0f,  1.0f,
	};
	const GLfloat tex[] = {
		0.0f, 1.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f,
	};

	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);

	GLint position_attr = glGetAttribLocation(program, "in_position");
	GLint tex_attr = glGetAttribLocation(program, "in_tex");

	glVertexAttribPointer(position_attr, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(tex_attr, 2, GL_FLOAT, GL_FALSE, 0, tex);

	glEnableVertexAttribArray(position_attr);
	glEnableVertexAttribArray(tex_attr);

	GLint phase_unif = glGetUniformLocation(program, "un_phase");
	glUniform1f(phase_unif, phase);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void render_blur(bool vert)
{
	GLint viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, viewport);
	const GLfloat verts[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		 1.0f,  1.0f,
	};
	const GLfloat tex[] = {
		0.0f, 1.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f,
	};
	const GLfloat texdisph[] = {
		-20.0f / viewport[2], 0.0f,
		-10.0f / viewport[2], 0.0f,
		-0.0f  / viewport[2], 0.0f,
		-10.0f / viewport[2], 0.0f,
		-20.0f / viewport[2], 0.0f,
	};
	const GLfloat texdispv[] = {
		0.0f, -20.0f / viewport[3],
		0.0f, -10.0f / viewport[3],
		0.0f,  -0.0f / viewport[3],
		0.0f, -10.0f / viewport[3],
		0.0f, -20.0f / viewport[3],
	};
	const GLfloat coefs[] = {
		0.1f, 0.25f, 0.3f, 0.25f, 0.1f,
	};

	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);

	GLint position_attr = glGetAttribLocation(program, "in_position");
	GLint tex_attr = glGetAttribLocation(program, "in_tex");

	glVertexAttribPointer(position_attr, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(tex_attr, 2, GL_FLOAT, GL_FALSE, 0, tex);

	glEnableVertexAttribArray(position_attr);
	glEnableVertexAttribArray(tex_attr);

	GLint texdiff_unif = glGetUniformLocation(program, "texdisp");
	if (vert)
		glUniform2fv(texdiff_unif, 7, texdispv);
	else
		glUniform2fv(texdiff_unif, 7, texdisph);

	GLint coefs_unif = glGetUniformLocation(program, "coefs");
	glUniform1fv(coefs_unif, 7, coefs);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gl_surf_render(EGLDisplay dpy, EGLContext ctx,
		    struct my_surface *s,
		    bool col, bool anim, bool blur)
{
	if (!eglMakeCurrent(dpy, s->egl_surface, s->egl_surface, ctx))
		return;

	glViewport(0, 0, (GLint) s->base.width, (GLint) s->base.height);

	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo[0]);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(normal_program);
	if (col)
		glClearColor(0.4f, 0.4f, 0.4f, 0.4f);
	else
		glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	render(s->rot);
	if (anim)
		s->rot += 0.01f;
	if (s->rot > 2.0f * M_PI)
		s->rot -= 2.0f * M_PI;

	if (blur) {
		glBindFramebuffer(GL_FRAMEBUFFER, s->fbo[1]);
		glBindTexture(GL_TEXTURE_2D, s->tex[0]);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	glBindTexture(GL_TEXTURE_2D, s->tex[0]);
	glUseProgram(ripple_program);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	render_ripple(s->phase);
	s->phase += 0.2f;
	if (s->phase > 2.0f * M_PI)
		s->phase -= 2.0f * M_PI;

	if (blur) {
		glBindFramebuffer(GL_FRAMEBUFFER, s->fbo[0]);
		glBindTexture(GL_TEXTURE_2D, s->tex[1]);
		glUseProgram(blur_program);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		render_blur(false);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, s->tex[0]);
		glUseProgram(blur_program);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		render_blur(true);
	}
}

void gl_fini(void)
{
	glDeleteProgram(ripple_program);
	glDeleteProgram(normal_program);
	glDeleteProgram(blur_program);
}

bool gl_init(void)
{
	normal_program = create_normal_program();
	ripple_program = create_ripple_program();
	blur_program = create_blur_program();

	return normal_program && ripple_program && blur_program;
}

void gl_surf_fini(EGLDisplay dpy, struct my_surface *s)
{
	glDeleteFramebuffers(2, s->fbo);
	glDeleteTextures(2, s->tex);

	eglDestroySurface(dpy, s->egl_surface);
}

bool gl_surf_init(EGLDisplay dpy, EGLConfig config, struct my_surface *s)
{
	GLint internal_format;
	GLenum type, format;

	s->egl_surface = eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)s->base.gbm_surface, NULL);
	if (s->egl_surface == EGL_NO_SURFACE)
		return false;

	switch (32) {
	case 12:
		internal_format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4;
		format = GL_RGBA;
		break;
	case 15:
		internal_format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_5_5_5_1;
		format = GL_RGBA;
		break;
	case 16:
		internal_format = GL_RGB;
		type = GL_UNSIGNED_SHORT_5_6_5;
		format = GL_RGB;
		break;
	case 24:
		internal_format = GL_RGB;
		type = GL_UNSIGNED_BYTE;
		format = GL_RGB;
		break;
	case 32:
		internal_format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		format = GL_RGBA;
		break;
	}

	glGenFramebuffers(2, s->fbo);
	glGenTextures(2, s->tex);

	glBindTexture(GL_TEXTURE_2D, s->tex[0]);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, s->base.width, s->base.height, 0, format, type, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo[0]);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex[0], 0);

	glBindTexture(GL_TEXTURE_2D, s->tex[1]);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, s->base.width, s->base.height, 0, format, type, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo[1]);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex[1], 0);

	return true;
}
