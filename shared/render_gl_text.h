#ifndef RENDER_GL_TEXT_H
#define RENDER_GL_TEXT_H

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

// Calculate the width of a text string at a given font size
// Returns the width in pixels
float gl_calculate_text_width(const char *text, int font_size);

// Draw simple text at the given position with the specified font size
// Uses the currently set color (via gl_set_color or gl_set_color_alpha)
// Position (x, y) is the baseline of the text
void gl_draw_text_simple(const char *text, int x, int y, int font_size);

void draw_vertices(Vertex *verts, int count, GLenum mode);

#endif // RENDER_GL_TEXT_H
