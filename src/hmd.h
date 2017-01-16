/*
 * Copyright © 2014 Chameleon
 * Copyright © 2017 James Sarrett
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HMD_H_
#define _HMD_H_

#include "config.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

struct EyeArg {
	GLuint framebuffer;
	struct weston_matrix projection;
	GLuint indexBuffer;
	GLuint indexBufferCount;
	GLuint vertexBuffer;
	GLuint uvsBuffer;
	GLuint texture;
	int textureWidth;
	int textureHeight;
};

struct scene_ {
	GLuint vertexBuffer;
	GLuint uvsBuffer;
};

struct distortion_shader_ {
	GLuint program;
	GLint Position;
	GLint TexCoord0;
	GLint eyeTexture;
};

struct eye_shader_ {
	GLuint program;
	GLint Position;
	GLint TexCoord0;
	GLint Projection;
	GLint ModelView;
	GLint virtualScreenTexture;
};

struct hmd_state {
	/*
	EGLSurface pbuffer;
	EGLSurface orig_surface;
	EGLConfig egl_config;
	EGLContext egl_context;
	EGLDisplay egl_display;
	GLuint texture;
	*/
	struct weston_output *output;
	GLuint redirectedFramebuffer;
	GLuint fbTexture;
	struct distortion_shader_ *distortion_shader;
	struct eye_shader_ *eye_shader;
	struct scene_ *scene;
	int n_eyes;
	struct EyeArg *eyeArgs;

	int enabled;

	//virtual screen geometry
	float screen_z;
	float screen_scale;
	int screen_width;
	int screen_height;
};
#endif
