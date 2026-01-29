#ifndef RENDER_GL_TEXT_H
#define RENDER_GL_TEXT_H

// Calculate the width of a text string at a given font size
// Returns the width in pixels
static float gl_calculate_text_width(const char *text, int font_size);

// Draw simple text at the given position with the specified font size
// Uses the currently set color (via gl_set_color or gl_set_color_alpha)
// Position (x, y) is the baseline of the text
void gl_draw_text_simple(const char *text, int x, int y, int font_size);

#endif // RENDER_GL_TEXT_H
