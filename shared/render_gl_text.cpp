#include <GL/glew.h>
#include <GL/gl.h>
#include "render_gl_text.h"
#include "Monospace.h"

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
    }
}

void draw_vertices(Vertex *verts, int count, GLenum mode) {
    glUseProgram(gl_state.program);
    
    GLint proj_loc = glGetUniformLocation(gl_state.program, "projection");
    glUniformMatrix4fv(proj_loc, 1, GL_FALSE, gl_state.projection.m);
    
    glBindBuffer(GL_ARRAY_BUFFER, gl_state.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), verts);
    
    glBindVertexArray(gl_state.vao);
    glDrawArrays(mode, 0, count);
}
