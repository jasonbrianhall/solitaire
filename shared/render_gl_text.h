#ifndef RENDER_GL_TEXT_H
#define RENDER_GL_TEXT_H

#include <GL/gl.h>

typedef struct {
    float m[16];
} Mat4;

typedef struct {
    float x, y;
    float r, g, b, a;
} Vertex;

typedef struct {
    GLuint program;
    GLuint vao;
    GLuint vbo;
    Mat4 projection;
    float color[4];
} GLRenderState;

static GLRenderState gl_state = {0};

// Initialize text rendering (call once at startup)
void gl_init(void);

// Set up 2D projection matrix for the viewport
void gl_setup_2d_projection(int width, int height);

// Set text color (alpha = 1.0)
void gl_set_color(float r, float g, float b);

// Set text color with alpha
void gl_set_color_alpha(float r, float g, float b, float a);

// Calculate the width of a text string at a given font size
// Returns the width in pixels
float gl_calculate_text_width(const char *text, int font_size);

// Draw simple text at the given position with the specified font size
// Uses the currently set color (via gl_set_color or gl_set_color_alpha)
// Position (x, y) is the baseline of the text
void gl_draw_text_simple(const char *text, int x, int y, int font_size);

static void draw_vertices(Vertex *verts, int count, GLenum mode);

#endif // RENDER_GL_TEXT_H
