#include <GL/glew.h>
#include <GL/gl.h>
#include "render_gl_text.h"
#include "Monospace.h"
#include <stdio.h>
#include <stddef.h>

static Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

static Mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    Mat4 m = mat4_identity();
    m.m[0] = 2.0f / (right - left);
    m.m[5] = 2.0f / (top - bottom);
    m.m[10] = -2.0f / (far - near);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -(far + near) / (far - near);
    return m;
}

// Shader sources
static const char *vertex_shader = 
    "#version 330 core\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec4 color;\n"
    "uniform mat4 projection;\n"
    "out vec4 vertexColor;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(position, 0.0, 1.0);\n"
    "    vertexColor = color;\n"
    "}\n";

static const char *fragment_shader =
    "#version 330 core\n"
    "in vec4 vertexColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vertexColor;\n"
    "}\n";

static GLuint compile_shader(const char *src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    
    int success;
    char log[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, log);
        fprintf(stderr, "[GL] Text shader compile error: %s\n", log);
    }
    return shader;
}

static GLuint create_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(vs_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fs_src, GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    
    int success;
    char log[512];
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(prog, 512, NULL, log);
        fprintf(stderr, "[GL] Text program link error: %s\n", log);
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void gl_init(void) {
    if (gl_state.program) return;  // Already initialized
    
    fprintf(stderr, "[GL] Initializing text renderer\n");
    
    gl_state.program = create_program(vertex_shader, fragment_shader);
    
    glGenVertexArrays(1, &gl_state.vao);
    glGenBuffers(1, &gl_state.vbo);
    
    glBindVertexArray(gl_state.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl_state.vbo);
    glBufferData(GL_ARRAY_BUFFER, 100000 * sizeof(Vertex), NULL, GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    gl_state.color[0] = 1.0f;
    gl_state.color[1] = 1.0f;
    gl_state.color[2] = 1.0f;
    gl_state.color[3] = 1.0f;
    
    fprintf(stderr, "[GL] Text renderer initialized\n");
}

void gl_setup_2d_projection(int width, int height) {
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    gl_state.projection = mat4_ortho(0, width, height, 0, -1, 1);
}

void gl_set_color(float r, float g, float b) {
    gl_state.color[0] = r;
    gl_state.color[1] = g;
    gl_state.color[2] = b;
    gl_state.color[3] = 1.0f;
}

void gl_set_color_alpha(float r, float g, float b, float a) {
    gl_state.color[0] = r;
    gl_state.color[1] = g;
    gl_state.color[2] = b;
    gl_state.color[3] = a;
}

// Calculate actual text width based on glyph metrics
// Use advance field which includes proper character spacing
float gl_calculate_text_width(const char *text, int font_size) {
    if (!text || !text[0]) return 0.0f;
    
    float ref_height = (float)FONT_MAX_HEIGHT;
    float scale = (float)font_size / ref_height;
    float width = 0.0f;
    
    for (int i = 0; text[i]; i++) {
        unsigned char ch = (unsigned char)text[i];
        const GlyphData *glyph = get_glyph(ch);
        if (glyph) {
            // Use advance field (includes character width + proper spacing)
            width += (float)glyph->advance * scale;
        }
    }
    
    return width;
}

void gl_draw_text_simple(const char *text, int x, int y, int font_size) {
    if (!text || !text[0]) return;
    
    float ref_height = (float)FONT_MAX_HEIGHT;
    float scale = (float)font_size / ref_height;
    
    float current_x = (float)x;
    float baseline_y = (float)y;
    
    // Build vertex array
    static Vertex verts[100000];
    int vert_count = 0;
    
    for (int i = 0; text[i] && vert_count < 99990; i++) {
        unsigned char ch = (unsigned char)text[i];
        const GlyphData *glyph = get_glyph(ch);
        
        float glyph_advance = glyph ? (float)glyph->advance * scale : (scale * 17.0f);
        
        if (!glyph || !glyph->bitmap) {
            current_x += glyph_advance;
            continue;
        }
        
        float pixel_scale = scale;
        float glyph_top = baseline_y - (float)glyph->yoffset * scale;
        
        for (int row = 0; row < glyph->height; row++) {
            float pixel_y = glyph_top + row * pixel_scale;
            
            for (int col = 0; col < glyph->width; col++) {
                unsigned char pixel = glyph->bitmap[row * glyph->width + col];
                
                if (pixel > 3 && vert_count < 99994) {
                    float pixel_x = current_x + col * pixel_scale;
                    float alpha = gl_state.color[3] * ((float)pixel / 255.0f);
                    
                    float r = gl_state.color[0];
                    float g = gl_state.color[1];
                    float b = gl_state.color[2];
                    
                    // Triangle 1
                    verts[vert_count++] = {pixel_x, pixel_y, r, g, b, alpha};
                    verts[vert_count++] = {pixel_x + pixel_scale, pixel_y, r, g, b, alpha};
                    verts[vert_count++] = {pixel_x + pixel_scale, pixel_y + pixel_scale, r, g, b, alpha};
                    
                    // Triangle 2
                    verts[vert_count++] = {pixel_x, pixel_y, r, g, b, alpha};
                    verts[vert_count++] = {pixel_x + pixel_scale, pixel_y + pixel_scale, r, g, b, alpha};
                    verts[vert_count++] = {pixel_x, pixel_y + pixel_scale, r, g, b, alpha};
                }
            }
        }
        
        current_x += glyph_advance;
    }
    
    if (vert_count > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        draw_vertices(verts, vert_count, GL_TRIANGLES);
        glDisable(GL_BLEND);
    }
}

static void draw_vertices(Vertex *verts, int count, GLenum mode) {
    glUseProgram(gl_state.program);
    
    GLint proj_loc = glGetUniformLocation(gl_state.program, "projection");
    glUniformMatrix4fv(proj_loc, 1, GL_FALSE, gl_state.projection.m);
    
    glBindBuffer(GL_ARRAY_BUFFER, gl_state.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), verts);
    
    glBindVertexArray(gl_state.vao);
    glDrawArrays(mode, 0, count);
}
