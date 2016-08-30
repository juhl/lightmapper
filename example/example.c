#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define TRIANGLEPACKER_IMPLEMENTATION
#define TP_DEBUG_OUTPUT
#include "trianglepacker/trianglepacker.h"

#define LIGHTMAPPER_IMPLEMENTATION
#define LM_DEBUG_INTERPOLATION
#include "../lightmapper.h"

#define YO_IMPLEMENTATION
#define YO_NOIMG
#include "yocto_obj.h"

typedef struct
{
	GLuint program;
	GLint u_lightmap;
	GLint u_projection;
	GLint u_view;
	GLuint vao, vbo;

	lm_vec3 *positions;
	lm_vec2 *texcoords;
	int vertices;

	float *data;
	GLuint texture;
	int w, h;
} lm_scene;

static int initScene(lm_scene *scene);
static void drawScene(lm_scene *scene, float *view, float *projection);
static void destroyScene(lm_scene *scene);

/*static void tp_float_line(float *data, int w, int h, int c, lm_vec2 p0, lm_vec2 p1, float r, float g, float b)
{
	int x0 = (int)(p0.x * w);
	int y0 = (int)(p0.y * h);
	int x1 = (int)(p1.x * w);
	int y1 = (int)(p1.y * h);
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2, e2;
	for (;;)
	{
		float *p = data + (y0 * w + x0) * c;
		p[0] = r; p[1] = g; p[2] = b;

		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -dx) { err -= dy; x0 += sx; }
		if (e2 <  dy) { err += dx; y0 += sy; }
	}
}

static void tp_smooth_edge(lm_vec2 a0, lm_vec2 a1, lm_vec2 b0, lm_vec2 b1, float *data, int w, int h, int c)
{
	//tp_float_line(data, w, h, c, a0, b0, 1.0f, 0.0f, 0.0f);
	//tp_float_line(data, w, h, c, a1, b1, 0.0f, 1.0f, 0.0f);
	lm_vec2 s = lm_v2i(w, h);
	a0 = lm_mul2(a0, s);
	a1 = lm_mul2(a1, s);
	b0 = lm_mul2(b0, s);
	b1 = lm_mul2(b1, s);
	lm_vec2 ad = lm_sub2(a1, a0);
	lm_vec2 bd = lm_sub2(b1, b0);
	float l = lm_length2(ad);
	int iterations = (int)(l * 10.0f);
	float step = 1.0f / iterations;
	for (int i = 0; i <= iterations; i++)
	{
		float t = i * step;
		lm_vec2 a = lm_add2(a0, lm_scale2(ad, t));
		lm_vec2 b = lm_add2(b0, lm_scale2(bd, t));
		int ax = (int)roundf(a.x), ay = (int)roundf(a.y);
		int bx = (int)roundf(b.x), by = (int)roundf(b.y);
		for (int j = 0; j < c; j++)
		{
			float ac = data[(ay * w + ax) * c + j];
			float bc = data[(by * w + bx) * c + j];
			if (ac > 0.0f && bc > 0.0f)
			{
				float amount = (ac > 0.0f && bc > 0.0f) ? 0.5f : 1.0f;
				data[(ay * w + ax) * c + j] = data[(by * w + bx) * c + j] = amount * (ac + bc);
			}
		}
	}
}

static int tp_should_smooth(lm_vec3 *tria, lm_vec3 *trib)
{
	lm_vec3 n0 = lm_normalize3(lm_cross3(lm_sub3(tria[1], tria[0]), lm_sub3(tria[2], tria[0])));
	lm_vec3 n1 = lm_normalize3(lm_cross3(lm_sub3(trib[1], trib[0]), lm_sub3(trib[2], trib[0])));
	return lm_absf(lm_dot3(n0, n1)) > 0.5f; // TODO: make threshold an argument!
}

static void tp_smooth_edges(lm_vec3 *positions, lm_vec2 *texcoords, int vertices, float *data, int w, int h, int c)
{
	lm_vec3 bbmin = lm_v3(FLT_MAX, FLT_MAX, FLT_MAX);
	lm_vec3 bbmax = lm_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	int *hashmap = (int*)LM_CALLOC(vertices * 2, sizeof(int));
	for (int i = 0; i < vertices; i++)
	{
		bbmin = lm_min3(bbmin, positions[i]);
		bbmax = lm_max3(bbmax, positions[i]);
		hashmap[i * 2 + 0] = -1;
		hashmap[i * 2 + 1] = -1;
	}
	
	lm_vec3 bbscale = lm_v3(15.9f / bbmax.x, 15.9f / bbmax.y, 15.9f / bbmax.z);

	for (int i0 = 0; i0 < vertices; i0++)
	{
		int tri = i0 - (i0 % 3);
		int i1 = tri + ((i0 + 1) % 3);
		int i2 = tri + ((i0 + 2) % 3);
		lm_vec3 p = lm_mul3(lm_sub3(positions[i0], bbmin), bbscale);
		int hash = (281 * (int)p.x + 569 * (int)p.y + 1447 * (int)p.z) % (vertices * 2);
		while (hashmap[hash] >= 0)
		{
			int oi0 = hashmap[hash];
#define TP_EQUAL(a, b) lm_length3sq(lm_sub3(positions[a], positions[b])) < 0.00001f
			if (TP_EQUAL(oi0, i0))
			{
				int otri = oi0 - (oi0 % 3);
				int oi1 = otri + ((oi0 + 1) % 3);
				int oi2 = otri + ((oi0 + 2) % 3);
				if (TP_EQUAL(oi1, i1) && tp_should_smooth(positions + tri, positions + otri))
					tp_smooth_edge(texcoords[i0], texcoords[i1], texcoords[oi0], texcoords[oi1], data, w, h, c);
				//else if (TP_EQUAL(oi1, i2) && tp_should_smooth(positions + tri, positions + otri))
				//	tp_smooth_edge(texcoords[i0], texcoords[i2], texcoords[oi0], texcoords[oi1], data, w, h, c);
				else if (TP_EQUAL(oi2, i1) && tp_should_smooth(positions + tri, positions + otri))
					tp_smooth_edge(texcoords[i0], texcoords[i1], texcoords[oi0], texcoords[oi2], data, w, h, c);
			}
			if (++hash == vertices * 2)
				hash = 0;
		}
		hashmap[hash] = i0;
	}
}*/

static void injectDirectLighting(lm_scene *scene)
{
	for (int i = 400; i < 500 && i < scene->h; i++)
	{
		for (int j = 0; j < 512 && j < scene->w; j++)
		{
			scene->data[(i * scene->w + j) * 4 + 0] = 0.0f;
			scene->data[(i * scene->w + j) * 4 + 1] = 0.5f;
			scene->data[(i * scene->w + j) * 4 + 2] = 1.0f;
			scene->data[(i * scene->w + j) * 4 + 3] = 1.0f;
		}
	}
}

static float sky[] = { 1.0f, 0.8f, 0.3f };
static float bakeScale = 0.01f;

static int bake(lm_scene *scene)
{
	lm_context *ctx = lmCreate(
		128,                    // hemisphere resolution (power of two, max=512)
		0.0001f, 100.0f,        // zNear, zFar of hemisphere cameras
		bakeScale * sky[0], bakeScale * sky[1], bakeScale * sky[2], // background color (white for ambient occlusion)
		2, 0.01f);              // lightmap interpolation & threshold (small differences are interpolated rather than sampled)
	                            // check debug_interpolation.tga for an overview of sampled (red) vs interpolated (green) pixels.
	if (!ctx)
	{
		fprintf(stderr, "Error: Could not initialize lightmapper.\n");
		return 0;
	}
	
	double bakeStart = glfwGetTime();

	float *data = scene->data;
	int w = scene->w, h = scene->h;

	glBindTexture(GL_TEXTURE_2D, scene->texture); // load linear lit lightmap (no gamma correction)
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, data);

	memset(data, 0, sizeof(float) * w * h * 4); // clear lightmap before rendering to it.
	lmSetTargetLightmap(ctx, data, w, h, 4);

	lmSetGeometry(ctx, NULL,
		LM_FLOAT, scene->positions, 0,
		LM_FLOAT, scene->texcoords, 0,
		scene->vertices, LM_NONE, 0);

	int vp[4];
	float view[16], projection[16];
	double lastUpdateTime = 0.0;
	while (lmBegin(ctx, vp, view, projection))
	{
		// render to lightmapper framebuffer
		glViewport(vp[0], vp[1], vp[2], vp[3]);
		drawScene(scene, view, projection);

		// display progress every second (printf is expensive)
		double time = glfwGetTime();
		if (time - lastUpdateTime > 1.0)
		{
			lastUpdateTime = time;
			printf("\r%6.2f%%", lmProgress(ctx) * 100.0f);
			fflush(stdout);
		}

		lmEnd(ctx);
	}
	printf("\rFinished baking %d triangles.\n", scene->vertices / 3);
	printf("Baking took %.1fs.\n", (float)(glfwGetTime() - bakeStart));

	// postprocess texture
	float *temp = calloc(w * h * 4, sizeof(float));
	for (int i = 0; i < 1; i++)
	{
		//tp_smooth_edges(scene->positions, scene->texcoords, scene->vertices, data, w, h, 4);
	}
	lmImageSmooth(data, temp, w, h, 4);
	lmImageDilate(temp, data, w, h, 4);
	for (int i = 0; i < 4; i++)
	{
		lmImageDilate(data, temp, w, h, 4);
		lmImageDilate(temp, data, w, h, 4);
	}

	injectDirectLighting(scene);

	lmImageDilate(data, temp, w, h, 4); // just copy to temp
	lmImagePower(temp, w, h, 4, 1.0f / 2.2f, 0x7); // gamma correct color channels

	// save result to a file
	if (lmImageSaveTGAf("result.tga", temp, w, h, 4, 1.0f))
		printf("Saved result.tga\n");

	// upload result
	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, temp);

	free(temp);

	lmDestroy(ctx);
	return 1;
}

static void error_callback(int error, const char *description)
{
	fprintf(stderr, "Error: %s\n", description);
}

static void fpsCameraViewMatrix(GLFWwindow *window, float *view);
static void perspectiveMatrix(float *out, float fovy, float aspect, float zNear, float zFar);

int main(int argc, char* argv[])
{
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) return 1;
	glfwWindowHint(GLFW_RED_BITS, 8);
	glfwWindowHint(GLFW_GREEN_BITS, 8);
	glfwWindowHint(GLFW_BLUE_BITS, 8);
	glfwWindowHint(GLFW_ALPHA_BITS, 8);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);
	glfwWindowHint(GLFW_STENCIL_BITS, GLFW_DONT_CARE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
	glfwWindowHint(GLFW_SAMPLES, 4);
	GLFWwindow *window = glfwCreateWindow(1024, 768, "Lightmapping Example", NULL, NULL);
	if (!window) return 1;
	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
	glfwSwapInterval(1);

	lm_scene scene = {0};
	if (!initScene(&scene))
	{
		fprintf(stderr, "Could not initialize scene.\n");
		return 1;
	}

	printf("Baking Example.\n");
	printf("Use your mouse and the W, A, S, D, E, Q keys to navigate.\n");
	printf("Press SPACE to start baking one light bounce!\n");
	printf("This will take a few seconds and bake a lightmap illuminated by:\n");
	printf("1. The mesh itself (initially black with some emissive patches)\n");
	printf("2. The sky\n");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			bake(&scene);

		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);

		// camera for glfw window
		float view[16], projection[16];
		fpsCameraViewMatrix(window, view);
		perspectiveMatrix(projection, 45.0f, (float)w / (float)h, 0.01f, 100.0f);

		// draw to screen with a blueish sky
		glClearColor(sky[0], sky[1], sky[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		drawScene(&scene, view, projection);

		glfwSwapBuffers(window);
	}

	destroyScene(&scene);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

// helpers ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static GLuint loadProgram(const char *vp, const char *fp, const char **attributes, int attributeCount);

static int initScene(lm_scene *scene)
{
	// load obj
	yo_scene *yo = yo_load_obj("city.obj", true, false);
	if (!yo || !yo->nshapes)
	{
		fprintf(stderr, "Error loading obj file\n");
		return 0;
	}

	scene->vertices = 0;
	for (int i = 0; i < yo->nshapes; i++)
		scene->vertices += yo->shapes[i].nelems * 3;
	
	scene->positions = LM_CALLOC(scene->vertices, sizeof(lm_vec3));
	scene->texcoords = LM_CALLOC(scene->vertices, sizeof(lm_vec2));
	size_t positionsSize = scene->vertices * sizeof(lm_vec3);
	size_t texcoordsSize = scene->vertices * sizeof(lm_vec2);
	
	int n = 0;
	for (int i = 0; i < yo->nshapes; i++)
	{
		yo_shape *shape = yo->shapes + i;
		for (int j = 0; j < shape->nelems * 3; j++)
			scene->positions[n + j] = *(lm_vec3*)&shape->pos[shape->elem[j] * 3];
		n += shape->nelems * 3;
	}
	yo_free_scene(yo);
	
	// create lightmap texture atlas
	scene->w = 1024;
	scene->h = 1024;
	float scale = 0.0f;
	int success = tpPackIntoRect((float*)scene->positions, scene->vertices, scene->w, scene->h, 0, 2, (float*)scene->texcoords, &scale);
	if (!success)
	{
		fprintf(stderr, "Could not pack all triangles into the lightmap!\n");
		return 0;
	}
	printf("Scale: %f\n", scale);
	//Sleep(3000);
	//system("start debug_triangle_packing.tga");
	//return 0;
	
	// upload geometry to opengl
	glGenVertexArrays(1, &scene->vao);
	glBindVertexArray(scene->vao);

	glGenBuffers(1, &scene->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
	glBufferData(GL_ARRAY_BUFFER, positionsSize + texcoordsSize, NULL, GL_STATIC_DRAW);
	unsigned char *buffer = (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	assert(buffer);
	memcpy(buffer                , scene->positions, positionsSize);
	memcpy(buffer + positionsSize, scene->texcoords, texcoordsSize);
	glUnmapBuffer(GL_ARRAY_BUFFER);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)positionsSize);

	// create black lightmap textures
	scene->data = calloc(scene->w * scene->h * 4, sizeof(float));
	injectDirectLighting(scene);

	glGenTextures(1, &scene->texture);
	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scene->w, scene->h, 0, GL_RGBA, GL_FLOAT, scene->data);

	// load shader
	const char *vp =
		"#version 150 core\n"
		"in vec3 a_position;\n"
		"in vec2 a_texcoord;\n"
		"uniform mat4 u_view;\n"
		"uniform mat4 u_projection;\n"
		"out vec2 v_texcoord;\n"

		"void main()\n"
		"{\n"
		"gl_Position = u_projection * (u_view * vec4(a_position, 1.0));\n"
		"v_texcoord = a_texcoord;\n"
		"}\n";

	const char *fp =
		"#version 150 core\n"
		"in vec2 v_texcoord;\n"
		"uniform sampler2D u_lightmap;\n"
		"out vec4 o_color;\n"

		"void main()\n"
		"{\n"
		"o_color = vec4(texture(u_lightmap, v_texcoord).rgb, gl_FrontFacing ? 1.0 : 0.0);\n"
		"}\n";

	const char *attribs[] =
	{
		"a_position",
		"a_texcoord"
	};

	scene->program = loadProgram(vp, fp, attribs, 2);
	if (!scene->program)
	{
		fprintf(stderr, "Error loading shader\n");
		return 0;
	}
	scene->u_view = glGetUniformLocation(scene->program, "u_view");
	scene->u_projection = glGetUniformLocation(scene->program, "u_projection");
	scene->u_lightmap = glGetUniformLocation(scene->program, "u_lightmap");

	return 1;
}

static void drawScene(lm_scene *scene, float *view, float *projection)
{
	glEnable(GL_DEPTH_TEST);

	glUseProgram(scene->program);
	glUniform1i(scene->u_lightmap, 0);
	glUniformMatrix4fv(scene->u_projection, 1, GL_FALSE, projection);
	glUniformMatrix4fv(scene->u_view, 1, GL_FALSE, view);

	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glBindVertexArray(scene->vao);
	glDrawArrays(GL_TRIANGLES, 0, scene->vertices);
}

static void destroyScene(lm_scene *scene)
{
	glDeleteVertexArrays(1, &scene->vao);
	glDeleteBuffers(1, &scene->vbo);
	glDeleteTextures(1, &scene->texture);
	glDeleteProgram(scene->program);
	free(scene->positions);
	free(scene->texcoords);
	free(scene->data);
}

static GLuint loadShader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	if (shader == 0)
	{
		fprintf(stderr, "Could not create shader!\n");
		return 0;
	}
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint compiled;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		fprintf(stderr, "Could not compile shader!\n");
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen)
		{
			char* infoLog = (char*)malloc(infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			fprintf(stderr, "%s\n", infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}
static GLuint loadProgram(const char *vp, const char *fp, const char **attributes, int attributeCount)
{
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vp);
	if (!vertexShader)
		return 0;
	GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fp);
	if (!fragmentShader)
	{
		glDeleteShader(vertexShader);
		return 0;
	}

	GLuint program = glCreateProgram();
	if (program == 0)
	{
		fprintf(stderr, "Could not create program!\n");
		return 0;
	}
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
    
    for (int i = 0; i < attributeCount; i++)
        glBindAttribLocation(program, i, attributes[i]);
    
	glLinkProgram(program);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	GLint linked;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		fprintf(stderr, "Could not link program!\n");
		GLint infoLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen)
		{
			char* infoLog = (char*)malloc(sizeof(char) * infoLen);
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			fprintf(stderr, "%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

#define LME_PI 3.14159265358979323846f

static void multiplyMatrices(float *out, float *a, float *b)
{
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			out[y * 4 + x] = a[x] * b[y * 4] + a[4 + x] * b[y * 4 + 1] + a[8 + x] * b[y * 4 + 2] + a[12 + x] * b[y * 4 + 3];
}
static void translationMatrix(float *out, float x, float y, float z)
{
	out[ 0] = 1.0f; out[ 1] = 0.0f; out[ 2] = 0.0f; out[ 3] = 0.0f;
	out[ 4] = 0.0f; out[ 5] = 1.0f; out[ 6] = 0.0f; out[ 7] = 0.0f;
	out[ 8] = 0.0f; out[ 9] = 0.0f; out[10] = 1.0f; out[11] = 0.0f;
	out[12] = x;    out[13] = y;    out[14] = z;    out[15] = 1.0f;
}
static void rotationMatrix(float *out, float angle, float x, float y, float z)
{
	angle *= LME_PI / 180.0f;
	float c = cosf(angle), s = sinf(angle), c2 = 1.0f - c;
	out[ 0] = x*x*c2 + c;   out[ 1] = y*x*c2 + z*s; out[ 2] = x*z*c2 - y*s; out[ 3] = 0.0f;
	out[ 4] = x*y*c2 - z*s; out[ 5] = y*y*c2 + c;   out[ 6] = y*z*c2 + x*s; out[ 7] = 0.0f;
	out[ 8] = x*z*c2 + y*s; out[ 9] = y*z*c2 - x*s; out[10] = z*z*c2 + c;   out[11] = 0.0f;
	out[12] = 0.0f;         out[13] = 0.0f;         out[14] = 0.0f;         out[15] = 1.0f;
}
static void transformPosition(float *out, float *m, float *p)
{
	float d = 1.0f / (m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]);
	out[2] =     d * (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
	out[1] =     d * (m[1] * p[0] + m[5] * p[1] + m[ 9] * p[2] + m[13]);
	out[0] =     d * (m[0] * p[0] + m[4] * p[1] + m[ 8] * p[2] + m[12]);
}
static void transposeMatrix(float *out, float *m)
{
	out[ 0] = m[0]; out[ 1] = m[4]; out[ 2] = m[ 8]; out[ 3] = m[12];
	out[ 4] = m[1]; out[ 5] = m[5]; out[ 6] = m[ 9]; out[ 7] = m[13];
	out[ 8] = m[2]; out[ 9] = m[6]; out[10] = m[10]; out[11] = m[14];
	out[12] = m[3]; out[13] = m[7]; out[14] = m[11]; out[15] = m[15];
}
static void perspectiveMatrix(float *out, float fovy, float aspect, float zNear, float zFar)
{
	float f = 1.0f / tanf(fovy * LME_PI / 360.0f);
	float izFN = 1.0f / (zNear - zFar);
	out[ 0] = f / aspect; out[ 1] = 0.0f; out[ 2] = 0.0f;                       out[ 3] = 0.0f;
	out[ 4] = 0.0f;       out[ 5] = f;    out[ 6] = 0.0f;                       out[ 7] = 0.0f;
	out[ 8] = 0.0f;       out[ 9] = 0.0f; out[10] = (zFar + zNear) * izFN;      out[11] = -1.0f;
	out[12] = 0.0f;       out[13] = 0.0f; out[14] = 2.0f * zFar * zNear * izFN; out[15] = 0.0f;
}

static void fpsCameraViewMatrix(GLFWwindow *window, float *view)
{
	// initial camera config
	static float position[] = { 0.0f, 0.3f, 1.5f };
	static float rotation[] = { 0.0f, 0.0f };

	// mouse look
	static double lastMouse[] = { 0.0, 0.0 };
	double mouse[2];
	glfwGetCursorPos(window, &mouse[0], &mouse[1]);
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
	{
		rotation[0] += (float)(mouse[1] - lastMouse[1]) * -0.2f;
		rotation[1] += (float)(mouse[0] - lastMouse[0]) * -0.2f;
	}
	lastMouse[0] = mouse[0];
	lastMouse[1] = mouse[1];

	float rotationY[16], rotationX[16], rotationYX[16];
	rotationMatrix(rotationX, rotation[0], 1.0f, 0.0f, 0.0f);
	rotationMatrix(rotationY, rotation[1], 0.0f, 1.0f, 0.0f);
	multiplyMatrices(rotationYX, rotationY, rotationX);

	// keyboard movement (WSADEQ)
	float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 0.1f : 0.01f;
	float movement[3] = {0};
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) movement[2] -= speed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) movement[2] += speed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) movement[0] -= speed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) movement[0] += speed;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) movement[1] -= speed;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) movement[1] += speed;

	float worldMovement[3];
	transformPosition(worldMovement, rotationYX, movement);
	position[0] += worldMovement[0];
	position[1] += worldMovement[1];
	position[2] += worldMovement[2];

	// construct view matrix
	float inverseRotation[16], inverseTranslation[16];
	transposeMatrix(inverseRotation, rotationYX);
	translationMatrix(inverseTranslation, -position[0], -position[1], -position[2]);
	multiplyMatrices(view, inverseRotation, inverseTranslation); // = inverse(translation(position) * rotationYX);
}
