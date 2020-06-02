#include <stdio.h>
#include <stdlib.h>

#include <math.h>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include "gl_platform.h"
#include "gl.h"

#include "shader_stuff.h"


static EGLDisplay edpy;
static EGLSurface esfc;
static EGLContext ectxt;

static int saved_texture_width;
static int saved_texture_height;

/* for external flips */
void *gl_es_display;
void *gl_es_surface;


static float vertex_coords[] = {
	-1.0f,  1.0f,  0.0f, // 0    0  1
	 1.0f,  1.0f,  0.0f, // 1  ^
	-1.0f, -1.0f,  0.0f, // 2  | 2  3
	 1.0f, -1.0f,  0.0f, // 3  +-->
};

static float orig_texture_coords[] = {
	-0.5f, -0.5f, 
	0.5f, -0.5f, 
	-0.5f, 0.5f, 
	0.5f, 0.5f, 
};

static float texture_coords[] = {
	0.0f, 0.0f, // we flip this:
	1.0f, 0.0f, // v^
	0.0f, 1.0f, //  |  u
	1.0f, 1.0f, //  +-->
};


static int gl_have_error(const char *name)
{
	GLenum e = glGetError();
	if (e != GL_NO_ERROR) {
		printf("GL error: %s %x\n", name, e);
		return 1;
	}
	return 0;
}

static int gles_have_error(const char *name)
{
	EGLint e = eglGetError();
	if (e != EGL_SUCCESS) {
		printf("%s %x\n", name, e);
		return 1;
	}
	return 0;
}

GLuint texture_name = 0;
GLuint texture_name2 = 0;
int texture_id = 0;
void *texture_mem = NULL;
void *texture_mem2 = NULL;

int gl_init(void *display, void *window, int *quirks, int texture_width, int texture_height)
{
    EGLConfig ecfg = NULL;
    EGLint num_config;
    int retval = -1;
    int ret;
	
   static const EGLint config_attributes[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };
   
   static const EGLint context_attributes[] = 
   {
#ifdef SHADER_SUPPORT
      EGL_CONTEXT_CLIENT_VERSION, 2,
#else
      EGL_CONTEXT_CLIENT_VERSION, 1,
#endif
      EGL_NONE
   };

	saved_texture_width = texture_width;
	saved_texture_height = texture_height;

	// gl_platform_init() does Raspi-specific stuff like bcm_host_init()
	ret = gl_platform_init(&display, &window, quirks);
	if (ret != 0) {
		printf("gl_platform_init failed with %d\n", ret);
		goto out;
	}

	texture_mem = calloc(1, texture_width * texture_height * 2);
	if (texture_mem == NULL) {
		printf("OOM\n");
		goto out;
	}

	texture_mem2 = calloc(1, texture_width * texture_height * 2);
	if (texture_mem2 == NULL) {
		printf("OOM\n");
		goto out;
	}

	edpy = eglGetDisplay((EGLNativeDisplayType)display);
	if (edpy == EGL_NO_DISPLAY) {
		printf("Failed to get EGL display\n");
		goto out;
	}

	if (!eglInitialize(edpy, NULL, NULL)) {
		printf("Failed to initialize EGL\n");
		goto out;
	}

	if (!eglChooseConfig(edpy, config_attributes, &ecfg, 1, &num_config)) {
		printf("Failed to choose config (%x)\n", eglGetError());
		goto out;
	}

	if (ecfg == NULL || num_config == 0) {
		printf("No EGL configs available\n");
		goto out;
	}

	esfc = eglCreateWindowSurface(edpy, ecfg,
		(EGLNativeWindowType)window, NULL);
	if (esfc == EGL_NO_SURFACE) {
		printf("Unable to create EGL surface (%x)\n",
			eglGetError());
		goto out;
	}

	ectxt = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, context_attributes);
	if (ectxt == EGL_NO_CONTEXT) {
		printf("Unable to create EGL context (%x)\n",
			eglGetError());
		goto out;
	}

	eglMakeCurrent(edpy, esfc, esfc, ectxt);

#ifndef SHADER_SUPPORT
	glEnable(GL_TEXTURE_2D); // for old fixed-function pipeline
#endif
	//if (gl_have_error("glEnable(GL_TEXTURE_2D)")) goto out;

	glGenTextures(1, &texture_name2);
	if (gl_have_error("glGenTextures")) goto out;

	glBindTexture(GL_TEXTURE_2D, texture_name2);
	if (gl_have_error("glBindTexture")) goto out;

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_width, texture_height, 0, GL_RGB,
		GL_UNSIGNED_SHORT_5_6_5,texture_mem2);
	if (gl_have_error("glTexImage2D")) goto out;

	// no mipmaps
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenTextures(1, &texture_name);
	if (gl_have_error("glGenTextures")) goto out;

	glBindTexture(GL_TEXTURE_2D, texture_name);
	if (gl_have_error("glBindTexture")) goto out;

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_width, texture_height, 0, GL_RGB,
		GL_UNSIGNED_SHORT_5_6_5, texture_mem);
	if (gl_have_error("glTexImage2D")) goto out;

	// no mipmaps
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//glViewport(0, 0, 512, 512);
	glLoadIdentity();
	glFrontFace(GL_CW);
	glEnable(GL_CULL_FACE);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_VERTEX_ARRAY);

	if (gl_have_error("init"))
		goto out;

	gl_es_display = (void *)edpy;
	gl_es_surface = (void *)esfc;
	retval = 0;

	int shader_stuff_result;
#ifdef SHADER_SUPPORT
	shader_stuff_result = shader_stuff_init();
	shader_stuff_result = shader_stuff_reload_shaders();
	shader_stuff_result = shader_stuff_set_data(vertex_coords, texture_coords, texture_name);
#endif

out:
	//free(tmp_texture_mem);
	return retval;
}

static int framecount = 0;

int gl_flip(const void *fb, int w, int h)
{
	static int old_w, old_h;

#ifdef SHADER_SUPPORT
	if (framecount % 60 == 0)
	{
//		printf("gl_flip() w: %d, h: %d\n", w, h);
	}
	
	if (framecount % 30 == 0)
	{
		if (shader_stuff_shader_needs_reload()) {
			 shader_stuff_reload_shaders();
			 // shader_stuff_set_data(vertex_coords, texture_coords, texture_name);
			
		 }
	}
#endif

	framecount++;
	float floattime = (framecount * 0.04f);

	if (fb != NULL) {
		if (w != old_w || h != old_h) {
			float f_w = (float)w / saved_texture_width;
			float f_h = (float)h / saved_texture_height;
			texture_coords[1*2 + 0] = f_w;
			texture_coords[2*2 + 1] = f_h;
			texture_coords[3*2 + 0] = f_w;
			texture_coords[3*2 + 1] = f_h;
			old_w = w;
			old_h = h;
		} 
/*
// This code makes the amiga screen spinning (wtf ?)
		float rotmat[4]; // 2d rotation matrix
		rotmat[0] = cos(floattime);
		rotmat[1] = sin(floattime);
		rotmat[2] = -sin(floattime);
		rotmat[3] = cos(floattime);

		for (int i=0; i<4; i++) {
				float f_w = (float)w / saved_texture_width;
				float f_h = (float)h / saved_texture_height;
				float x = orig_texture_coords[i*2 + 0] * f_w;
				float y = orig_texture_coords[i*2 + 1] * f_h;
				texture_coords[i*2 + 0] = 
					f_w * 0.5f + (x * rotmat[0] + y * rotmat[1]);
				texture_coords[i*2 + 1] = 
					f_h * 0.5f + (x * rotmat[2] + y * rotmat[3]);

		}
*/

		if (texture_id == 0)
		{
			glBindTexture(GL_TEXTURE_2D, texture_name);
			texture_id = 1;
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, texture_name2);
			texture_id = 0;
		}

		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
			GL_RGB, GL_UNSIGNED_SHORT_5_6_5, fb);
		if (gl_have_error("glTexSubImage2D"))
			return -1;
	} // if (fb != NULL)
#ifdef SHADER_SUPPORT
	shader_stuff_frame(framecount, w, h, 800, 480); // TODO! hard-coded output size
	if (gl_have_error("use program")) return -1;
#else
	glVertexPointer(3, GL_FLOAT, 0, vertex_coords);
	if (gl_have_error("glVertexPointer")) return -1;

	glTexCoordPointer(2, GL_FLOAT, 0, texture_coords);
	if (gl_have_error("glTexCoordPointer")) return -1;
#endif

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	//glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	if (gl_have_error("glDrawArrays")) return -1;

	eglSwapBuffers(edpy, esfc);
	if (gles_have_error("eglSwapBuffers")) return -1;

	return 0;
}

void gl_finish(void)
{
	eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(edpy, ectxt);
	ectxt = EGL_NO_CONTEXT;
	eglDestroySurface(edpy, esfc);
	esfc = EGL_NO_SURFACE;
	eglTerminate(edpy);
	edpy = EGL_NO_DISPLAY;

	gl_es_display = (void *)edpy;
	gl_es_surface = (void *)esfc;

	if (texture_mem != 0)
	{
		free(texture_mem);
		texture_mem = 0;
	}
	if (texture_mem2 != 0)
	{
		free(texture_mem2);
		texture_mem2 = 0;
	}

	gl_platform_finish();
}
