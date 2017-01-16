#include "postcompositor-hmd.h"
#include "compositor.h"
#include "gl-renderer.h"
#include <strings.h>
#include <wayland-server.h>
#include <GLES2/gl2.h>
#include <linux/input.h>

// HMD shaders

// Distortion shader
static const char* distortion_vertex_shader =
	"attribute vec2 Position;\n"
	"attribute vec2 TexCoord0;\n"
	"varying mediump vec2 oTexCoord0;\n"
	"void main() {\n"
	"  oTexCoord0 = TexCoord0;\n"
	"  gl_Position.xy = Position;\n"
	"  gl_Position.z = 0.0;\n"
	"  gl_Position.w = 1.0;\n"
	"}\n";
static const char* distortion_fragment_shader =
	"varying mediump vec2 oTexCoord0;\n"
	"uniform sampler2D Texture0;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(Texture0, oTexCoord0);\n"
	"}\n";
// Rendered scene (per eye) shader
static const char* eye_vertex_shader =
	"attribute vec3 Position;\n"
	"attribute vec2 TexCoord0;\n"
	"uniform mat4 Projection;\n"
	"uniform mat4 ModelView;\n"
	"varying mediump vec2 oTexCoord0;\n"
	"void main() {\n"
	"  oTexCoord0 = TexCoord0;\n"
	"  gl_Position = Projection * ModelView * vec4(Position, 1.0);\n"
	"}\n";

static const char* eye_fragment_shader =
	"varying mediump vec2 oTexCoord0;\n"
	"uniform sampler2D Texture0;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(Texture0, oTexCoord0);\n"
	"}\n";

// End of shaders


// Key handlers
static void
toggle_enable(struct weston_keyboard *keyboard, uint32_t time, uint32_t key, void *data)
{
	struct hmd_state *hmd = data;
	hmd->enabled = !hmd->enabled;
	//we switch out the entire framebuffe, so damage it whenever we change
	if (hmd->output)
		weston_output_damage(hmd->output);
}

static void
move_in(struct weston_keyboard *keyboard, uint32_t time,
		uint32_t key, void *data)
{
	struct hmd_state *hmd = data;
	hmd->screen_z += 0.1;
	if (hmd->output)
		weston_output_damage(hmd->output);
}

static void
move_out(struct weston_keyboard *keyboard, uint32_t time,
		uint32_t key, void *data)
{
	struct hmd_state *hmd = data;
	hmd->screen_z -= 0.1;
	if (hmd->output)
		weston_output_damage(hmd->output);
}

static void
scale_up(struct weston_keyboard *keyboard, uint32_t time,
		uint32_t key, void *data)
{
	struct hmd_state *hmd = data;
	hmd->screen_scale += 0.1;
	if (hmd->output)
		weston_output_damage(hmd->output);
}

static void
scale_down(struct weston_keyboard *keyboard, uint32_t time,
		uint32_t key, void *data)
{
	struct hmd_state *hmd = data;
	hmd->screen_scale -= 0.1;
	if (hmd->output)
		weston_output_damage(hmd->output);
}

int
create_hmd(struct hmd_state **hmd, struct weston_compositor *compositor)
//create_hmd(struct hmd_state **hmd, struct weston_compositor *compositor, EGLConfig egl_config, EGLDisplay egl_display, EGLSurface orig_surface, EGLContext egl_context)
{
	struct hmd_state *h;
	if ( (h = calloc(1, sizeof(struct hmd_state))) == NULL )
		return -1;
	/*
	h->egl_config = egl_config;
	h->egl_display = egl_display;
	h->orig_surface = orig_surface;
	h->egl_context = egl_context;
	 */
	*hmd = h;

	weston_compositor_add_key_binding(compositor, KEY_F11,
			MODIFIER_CTRL | MODIFIER_SUPER, toggle_enable, h);
	weston_compositor_add_key_binding(compositor, KEY_UP,
			MODIFIER_CTRL | MODIFIER_SUPER, move_in, h);
	weston_compositor_add_key_binding(compositor, KEY_DOWN,
			MODIFIER_CTRL | MODIFIER_SUPER, move_out, h);
	weston_compositor_add_key_binding(compositor, KEY_PAGEUP,
			MODIFIER_CTRL | MODIFIER_SUPER, scale_up, h);
	weston_compositor_add_key_binding(compositor, KEY_PAGEDOWN,
			MODIFIER_CTRL | MODIFIER_SUPER, scale_down, h);

	return 0;
}

void
show_error_(const char *file, int line)
{
	GLenum error = GL_NO_ERROR;
	error = glGetError();
	if(error != GL_NO_ERROR)
	{
		switch(error)
		{
			case GL_INVALID_OPERATION: weston_log("\tGL Error: GL_INVALID_OPERATION - %s : %i\n", file, line); break;
			case GL_INVALID_ENUM: weston_log("\tGL Error: GL_INVALID_ENUM - %s : %i\n", file, line); break;
			case GL_INVALID_VALUE: weston_log("\tGL Error: GL_INVALID_VALUE - %s : %i\n", file, line); break;
			case GL_OUT_OF_MEMORY: weston_log("\tGL Error: GL_OUT_OF_MEMORY - %s : %i\n", file, line); break;
			case GL_INVALID_FRAMEBUFFER_OPERATION: weston_log("\tGL Error: GL_INVALID_FRAMEBUFFER_OPERATION - %s : %i\n", file, line); break;
		}
	}
}

#define show_error() show_error_(__FILE__,__LINE__)

static GLuint
CreateShader(GLenum type, const char *shader_src)
{
	GLuint shader = glCreateShader(type);
	if(!shader)
		return 0;

	glShaderSource(shader, 1, &shader_src, NULL);
	glCompileShader(shader);

	GLint compiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if(!compiled)
	{
		GLint info_len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
		if(info_len > 1)
		{
			char *info_log = (char *)malloc(sizeof(char) * info_len);
			glGetShaderInfoLog(shader, info_len, NULL, info_log);
			weston_log("\tError compiling shader:\n\t%s\n", info_log);
			free(info_log);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint
CreateProgram(const char *vertex_shader_src, const char *fragment_shader_src)
{
	GLuint vertex_shader = CreateShader(GL_VERTEX_SHADER, vertex_shader_src);
	if(!vertex_shader)
		return 0;
	GLuint fragment_shader = CreateShader(GL_FRAGMENT_SHADER, fragment_shader_src);
	if(!fragment_shader)
	{
		glDeleteShader(vertex_shader);
		return 0;
	}

	GLuint program_object = glCreateProgram();
	if(!program_object)
		return 0;
	glAttachShader(program_object, vertex_shader);
	glAttachShader(program_object, fragment_shader);

	glLinkProgram(program_object);

	GLint linked = 0;
	glGetProgramiv(program_object, GL_LINK_STATUS, &linked);
	if(!linked)
	{
		GLint info_len = 0;
		glGetProgramiv(program_object, GL_INFO_LOG_LENGTH, &info_len);
		if(info_len > 1)
		{
			char *info_log = (char *)malloc(info_len);
			glGetProgramInfoLog(program_object, info_len, NULL, info_log);
			weston_log("\tError linking program:\n\t%s\n", info_log);
			free(info_log);
		}
		glDeleteProgram(program_object);
		return 0;
	}

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	return program_object;
}

int
setup_hmd(struct hmd_state *hmd, struct weston_output *output)
{
	hmd->enabled = 0;
	hmd->output = output;

	hmd->screen_z = -1.0;
	hmd->screen_scale = 1.0;

	hmd->n_eyes = 1;

	if ( (hmd->distortion_shader = calloc(1, sizeof *(hmd->distortion_shader))) == NULL)
		return -1;
	struct distortion_shader_ *d = hmd->distortion_shader;
	d->program = CreateProgram(distortion_vertex_shader, distortion_fragment_shader);
	d->Position = glGetAttribLocation(d->program, "Position");
	d->TexCoord0 = glGetAttribLocation(d->program, "TexCoord0");
	d->eyeTexture = glGetAttribLocation(d->program, "Texture0");

	if ( (hmd->eye_shader = calloc(1, sizeof *(hmd->eye_shader))) == NULL)
		return -1;
	struct eye_shader_ *e = hmd->eye_shader;
	e->program = CreateProgram(eye_vertex_shader, eye_fragment_shader);
	e->Position = glGetAttribLocation(d->program, "Position");
	e->TexCoord0 = glGetAttribLocation(d->program, "TexCoord0");
	e->Projection = glGetUniformLocation(e->program, "Projection");
	e->ModelView = glGetUniformLocation(e->program, "ModelView");
	e->virtualScreenTexture = glGetAttribLocation(d->program, "Texture0");

	if ( (hmd->scene = calloc(1, sizeof *(hmd->scene))) == NULL)
		return -1;
	glGenBuffers(1, &hmd->scene->vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, hmd->scene->vertexBuffer);
	/* for now just a flat card */
	static const GLfloat rectangle[] =
		{-1.0f, -1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f};
	glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle), rectangle, GL_STATIC_DRAW);

	glGenBuffers(1, &hmd->scene->uvsBuffer);
	static const GLfloat uvs[12] =
		{0.0, 1.0,
		1.0, 1.0,
		0.0, 0.0,
		1.0, 1.0,
		1.0, 0.0,
		0.0, 0.0};
	glBindBuffer(GL_ARRAY_BUFFER, hmd->scene->uvsBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);

	hmd->screen_width = output->width;
	hmd->screen_height = output->height;

	glGenTextures(1, &hmd->fbTexture);
	glBindTexture(GL_TEXTURE_2D, hmd->fbTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hmd->screen_width, hmd->screen_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &hmd->redirectedFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, hmd->redirectedFramebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hmd->fbTexture, 0); show_error();
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		switch(glCheckFramebufferStatus(GL_FRAMEBUFFER))
		{
			case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: weston_log("incomplete attachment\n"); break;
			case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: weston_log("incomplete dimensions\n"); break;
			case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: weston_log("incomplete missing attachment\n"); break;
			case GL_FRAMEBUFFER_UNSUPPORTED: weston_log("unsupported\n"); break;
		}

		weston_log("framebuffer not working\n");
		show_error();
		exit(1);
	}
	glClear(GL_COLOR_BUFFER_BIT);

	/*
	EGLint pbufferAttributes[] = {
	EGL_WIDTH,           hmd->width,
	EGL_HEIGHT,          hmd->height,
	EGL_TEXTURE_FORMAT,  EGL_TEXTURE_RGB,
	EGL_TEXTURE_TARGET,  EGL_TEXTURE_2D,
	EGL_NONE
	};

	hmd->pbuffer = eglCreatePbufferSurface(
	hmd->egl_display, rift->egl_config,
	pbufferAttributes);

	glGenTextures(1, &(hmd->texture));
	glBindTexture(GL_TEXTURE_2D, hmd->texture);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hmd->width, rift->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	eglMakeCurrent(hmd->egl_display, rift->pbuffer, rift->pbuffer, rift->egl_context);
	eglBindTexImage(hmd->egl_display, rift->pbuffer, EGL_BACK_BUFFER);
	eglMakeCurrent(hmd->egl_display, rift->orig_surface, rift->orig_surface, rift->egl_context);
	 */

	hmd->eyeArgs = calloc(hmd->n_eyes, sizeof(struct EyeArg));
	if (hmd->distortion_shader == NULL) {
		return -1;
	}
	int eye;
	for(eye = 0; eye < hmd->n_eyes; eye++)
	{
		struct EyeArg *eyeArg = &hmd->eyeArgs[eye];

		weston_matrix_init(&eyeArg->projection);
		eyeArg->projection.type = WESTON_MATRIX_TRANSFORM_OTHER;
		/* viewpot is 1 pixel scale */
		weston_matrix_scale(&eyeArg->projection, 1.0f/(hmd->screen_width/2), 1.0f/(hmd->screen_height/2), 1.0);
		eyeArg->textureWidth = hmd->screen_width/2;
		eyeArg->textureHeight = hmd->screen_height;

		glGenTextures(1, &eyeArg->texture);
		glBindTexture(GL_TEXTURE_2D, eyeArg->texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, eyeArg->textureWidth, eyeArg->textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &eyeArg->framebuffer); show_error();
		glBindFramebuffer(GL_FRAMEBUFFER, eyeArg->framebuffer); show_error();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eyeArg->texture, 0); show_error();
		if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			switch(glCheckFramebufferStatus(GL_FRAMEBUFFER))
			{
				case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: weston_log("incomplete attachment\n"); break;
				case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: weston_log("incomplete dimensions\n"); break;
				case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: weston_log("incomplete missing attachment\n"); break;
				case GL_FRAMEBUFFER_UNSUPPORTED: weston_log("unsupported\n"); break;
			}

			weston_log("framebuffer not working\n");
			show_error();
			exit(1);
		}
#if HMD_DEBUG_BACKGROUNDS
		if(eye)
			glClearColor(1.0, 0.0, 0.0, 1.0); show_error();
		else
			glClearColor(0.0, 1.0, 0.0, 1.0); show_error();
#else
		glClearColor(0.0, 0.0, 0.0, 1.0); show_error();
#endif
		glClear(GL_COLOR_BUFFER_BIT); show_error();

		/*
		EGLint eyePbufferAttributes[] = {
		EGL_WIDTH,           texRect.Size.w,
		EGL_HEIGHT,          texRect.Size.h,
		EGL_TEXTURE_FORMAT,  EGL_TEXTURE_RGB,
		EGL_TEXTURE_TARGET,  EGL_TEXTURE_2D,
		EGL_NONE
		};

		eyeArg.surface = eglCreatePbufferSurface(
		hmd->egl_display, rift->egl_config,
		eyePbufferAttributes);
		 */

		glGenBuffers(1, &eyeArg->indexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eyeArg->indexBuffer);
		//for now dummy rectangular mesh
		unsigned short mesh_idxs[6] = {0,1,2,3,4,5};
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mesh_idxs), mesh_idxs, GL_STATIC_DRAW);
		eyeArg->indexBufferCount = 6;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		uint i;

		glGenBuffers(1, &eyeArg->vertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, eyeArg->vertexBuffer);
		//for now dummy rectangular mesh
		GLfloat rect_mesh[12];
		float *r = rect_mesh;
		r[0] = -1.0f; r[1] = -1.0f;
		r[2] = 1.0f;  r[3] = -1.0f;
		r[4] = -1.0f; r[5] = 1.0f;
		r[6] = 1.0f;  r[7] = -1.0f;
		r[8] = 1.0f;  r[9] = 1.0f;
		r[10] = -1.0f; r[11] = 1.0f;
		glBufferData(GL_ARRAY_BUFFER, sizeof(rect_mesh), rect_mesh, GL_STATIC_DRAW);

		glGenBuffers(1, &eyeArg->uvsBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, eyeArg->uvsBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return 0;
}

int
render_hmd(struct hmd_state *hmd)
{
	int i;

	// copy hmd->pbuffer into hmd->texture
	/*
	eglMakeCurrent(hmd->egl_display, hmd->pbuffer, hmd->pbuffer, hmd->egl_context);
	//glClearColor(0.5, 0.0, 0.5, 1.0);
	//glClear(GL_COLOR_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, hmd->texture);
	eglReleaseTexImage(hmd->egl_display, hmd->pbuffer, EGL_BACK_BUFFER);
	eglBindTexImage(hmd->egl_display, hmd->pbuffer, EGL_BACK_BUFFER);
	eglMakeCurrent(hmd->egl_display, hmd->orig_surface, hmd->orig_surface, hmd->egl_context);
	 */
	// render eyes

	static int frameIndex = 0;
	++frameIndex;

	glEnable(GL_DEPTH_TEST);
	glUseProgram(hmd->eye_shader->program);
	for(i=0; i<hmd->n_eyes; i++)
	{
		int eye = 0;
		//render in order
		eye = i;
		struct EyeArg eyeArg = hmd->eyeArgs[eye];

		struct weston_matrix View;
		weston_matrix_init(&View);
		struct weston_matrix Model;
		weston_matrix_init(&Model);
		struct weston_matrix MV;
		weston_matrix_init(&MV);

		//translate to virtual screen distance
		weston_matrix_translate(&Model, 0, 0, hmd->screen_z);
		//shrink the quad to the virutal screen scale
		weston_matrix_scale(&Model, hmd->screen_scale, hmd->screen_scale, 1.0);

		weston_matrix_scale(&View, hmd->screen_width/2, hmd->screen_height/2, 1.0);
		weston_matrix_multiply(&MV, &Model);
		weston_matrix_multiply(&MV, &View);

		glBindFramebuffer(GL_FRAMEBUFFER, eyeArg.framebuffer);
		glViewport(0, 0, eyeArg.textureWidth, eyeArg.textureHeight);
#if HMD_DEBUG_BACKGROUNDS
		if(eye)
			glClearColor(0.0, 1.0, 1.0, 1.0);
		else
			glClearColor(1.0, 0.0, 1.0, 1.0);
#else
		glClearColor(0.0, 0.0, 0.2, 1.0);
#endif
		glClear(GL_COLOR_BUFFER_BIT);

		glUniform1i(hmd->eye_shader->virtualScreenTexture, 0);
		glUniformMatrix4fv(hmd->eye_shader->Projection, 1, GL_FALSE, &eyeArg.projection.d[0]);
		glUniformMatrix4fv(hmd->eye_shader->ModelView, 1, GL_FALSE, &MV.d[0]);

		glEnableVertexAttribArray(hmd->eye_shader->Position);
		glEnableVertexAttribArray(hmd->eye_shader->TexCoord0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hmd->fbTexture);
		glBindBuffer(GL_ARRAY_BUFFER, hmd->scene->vertexBuffer);
		glVertexAttribPointer(hmd->eye_shader->Position, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), NULL);
		glBindBuffer(GL_ARRAY_BUFFER, hmd->scene->uvsBuffer);

		glVertexAttribPointer(hmd->eye_shader->TexCoord0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDisableVertexAttribArray(hmd->eye_shader->Position);
		glDisableVertexAttribArray(hmd->eye_shader->TexCoord0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// render distortion
	glUseProgram(hmd->distortion_shader->program);
	glViewport(0, 0, hmd->screen_width, hmd->screen_height);

#if HMD_DEBUG_BACKGROUNDS
	glClearColor(0.0, 0.0, 1.0, 1.0);
#else
	glClearColor(0.0, 0.1, 0.0, 1.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	int eye;
	for(eye=0; eye<hmd->n_eyes; eye++)
	{
		struct EyeArg eyeArg = hmd->eyeArgs[eye];

		glUniform1i(hmd->distortion_shader->eyeTexture, 0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, eyeArg.texture);

		glBindBuffer(GL_ARRAY_BUFFER, eyeArg.vertexBuffer);
		glVertexAttribPointer(hmd->distortion_shader->Position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
		glEnableVertexAttribArray(hmd->distortion_shader->Position);

		glBindBuffer(GL_ARRAY_BUFFER, eyeArg.uvsBuffer);
		glVertexAttribPointer(hmd->distortion_shader->TexCoord0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
		glEnableVertexAttribArray(hmd->distortion_shader->TexCoord0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eyeArg.indexBuffer);

		glDrawElements(GL_TRIANGLES, eyeArg.indexBufferCount, GL_UNSIGNED_SHORT, 0);

		glDisableVertexAttribArray(hmd->distortion_shader->Position);
		glDisableVertexAttribArray(hmd->distortion_shader->TexCoord0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	glEnable(GL_DEPTH_TEST);

	return 0;
}
