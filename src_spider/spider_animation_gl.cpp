#include "spider.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// OpenGL 3.4 Shader sources
static const char *VERTEX_SHADER_GL = R"(
    #version 330 core
    layout(location = 0) in vec2 position;
    layout(location = 1) in vec2 texCoord;
    
    uniform mat4 projection;
    uniform mat4 view;
    uniform mat4 model;
    
    out VS_OUT {
        vec2 texCoord;
    } vs_out;
    
    void main()
    {
        gl_Position = projection * view * model * vec4(position, 0.0, 1.0);
        vs_out.texCoord = texCoord;
    }
)";

static const char *FRAGMENT_SHADER_GL = R"(
    #version 330 core
    in VS_OUT {
        vec2 texCoord;
    } fs_in;
    
    uniform sampler2D cardTexture;
    uniform float alpha;
    
    out vec4 FragColor;
    
    void main()
    {
        vec4 texColor = texture(cardTexture, fs_in.texCoord);
        FragColor = vec4(texColor.rgb, texColor.a * alpha);
    }
)";

static const char *FRAGMENT_SHADER_SIMPLE_GL = R"(
    #version 330 core
    
    uniform vec4 color;
    
    out vec4 FragColor;
    
    void main()
    {
        FragColor = color;
    }
)";

// ============================================================================
// CONTEXT VALIDATION AND INITIALIZATION FUNCTIONS
// ============================================================================

bool SolitaireGame::validateOpenGLContext_gl() {
    const GLubyte *version = glGetString(GL_VERSION);
    
    if (version == nullptr) {
        std::cerr << "\n" << std::string(70, '=') << std::endl;
        std::cerr << "CRITICAL ERROR: No OpenGL Context Available" << std::endl;
        std::cerr << std::string(70, '=') << std::endl;
        std::cerr << "  Problem: OpenGL function called without active GL context" << std::endl;
        std::cerr << "  Cause: " << std::endl;
        std::cerr << "    1. GLFW window not created or context not made current" << std::endl;
        std::cerr << "    2. GPU driver not installed or outdated" << std::endl;
        std::cerr << "    3. OpenGL disabled in system settings" << std::endl;
        std::cerr << "  Solution: Ensure GLFW context is created BEFORE calling OpenGL functions" << std::endl;
        std::cerr << std::string(70, '=') << "\n" << std::endl;
        return false;
    }
    
    std::cout << "✓ OpenGL Context Active: " << reinterpret_cast<const char*>(version) << std::endl;
    return true;
}

bool SolitaireGame::initializeGLEW_gl() {
    if (is_glew_initialized_) {
        std::cout << "✓ GLEW Already Initialized" << std::endl;
        return true;
    }
    
    if (!validateOpenGLContext_gl()) {
        std::cerr << "ERROR: Cannot initialize GLEW without OpenGL context" << std::endl;
        return false;
    }
    
    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    
    if (glewStatus != GLEW_OK) {
        std::cerr << "\n" << std::string(70, '=') << std::endl;
        std::cerr << "CRITICAL ERROR: GLEW Initialization Failed" << std::endl;
        std::cerr << std::string(70, '=') << std::endl;
        std::cerr << "  GLEW Error: " << glewGetErrorString(glewStatus) << std::endl;
        std::cerr << "  This usually means:" << std::endl;
        std::cerr << "    - GLFW context is not current (not bound to calling thread)" << std::endl;
        std::cerr << "    - Graphics driver is missing OpenGL extensions" << std::endl;
        std::cerr << "  Solution: Make sure glfw context is set as current thread context" << std::endl;
        std::cerr << std::string(70, '=') << "\n" << std::endl;
        return false;
    }
    
    is_glew_initialized_ = true;
    std::cout << "✓ GLEW Initialized: " << glewGetString(GLEW_VERSION) << std::endl;
    return true;
}

bool SolitaireGame::checkOpenGLCapabilities_gl() {
    std::cout << "\nChecking OpenGL Capabilities..." << std::endl;
    
    if (!validateOpenGLContext_gl()) {
        return false;
    }
    
    int major_version = 0, minor_version = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major_version);
    glGetIntegerv(GL_MINOR_VERSION, &minor_version);
    
    std::cout << "  OpenGL Version: " << major_version << "." << minor_version << std::endl;
    
    if (major_version < 3 || (major_version == 3 && minor_version < 3)) {
        std::cerr << "\n" << std::string(70, '=') << std::endl;
        std::cerr << "ERROR: OpenGL 3.3+ Required" << std::endl;
        std::cerr << std::string(70, '=') << std::endl;
        std::cerr << "  Your GPU supports: OpenGL " << major_version << "." << minor_version << std::endl;
        std::cerr << "  Solution: Update graphics drivers or use a newer GPU" << std::endl;
        std::cerr << std::string(70, '=') << "\n" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ OpenGL 3.3+ supported" << std::endl;
    
    std::cout << "  Checking required extensions..." << std::endl;
    
    if (!GLEW_ARB_vertex_array_object) {
        std::cerr << "    ✗ ARB_vertex_array_object NOT supported" << std::endl;
        return false;
    }
    std::cout << "    ✓ ARB_vertex_array_object" << std::endl;
    
    if (!GLEW_ARB_shader_objects) {
        std::cerr << "    ✗ ARB_shader_objects NOT supported" << std::endl;
        return false;
    }
    std::cout << "    ✓ ARB_shader_objects" << std::endl;
    
    if (!GLEW_ARB_vertex_shader) {
        std::cerr << "    ✗ ARB_vertex_shader NOT supported" << std::endl;
        return false;
    }
    std::cout << "    ✓ ARB_vertex_shader" << std::endl;
    
    if (!GLEW_ARB_fragment_shader) {
        std::cerr << "    ✗ ARB_fragment_shader NOT supported" << std::endl;
        return false;
    }
    std::cout << "    ✓ ARB_fragment_shader" << std::endl;
    
    logOpenGLInfo_gl();
    return true;
}

void SolitaireGame::logOpenGLInfo_gl() {
    std::cout << "\n" << std::string(70, '-') << std::endl;
    std::cout << "GPU INFORMATION" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    std::cout << "  Vendor:  " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "  Device:  " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "  Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "  Shading: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    
    int max_texture_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
    std::cout << "  Max Texture Units: " << max_texture_units << std::endl;
    
    int max_vao_attributes = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vao_attributes);
    std::cout << "  Max VAO Attributes: " << max_vao_attributes << std::endl;
    
    int max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    std::cout << "  Max Texture Size: " << max_texture_size << "x" << max_texture_size << std::endl;
    
    std::cout << std::string(70, '-') << "\n" << std::endl;
}

// ============================================================================
// SHADER COMPILATION WITH ERROR HANDLING
// ============================================================================

GLuint compileShader_gl(const char *source, GLenum shaderType) {
    std::cout << "  Compiling " << (shaderType == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT") 
              << " shader..." << std::endl;
    
    if (glGetString(GL_VERSION) == nullptr) {
        std::cerr << "    ✗ ERROR: No OpenGL context for shader compilation" << std::endl;
        return 0;
    }
    
    GLuint shader = glCreateShader(shaderType);
    
    if (shader == 0) {
        std::cerr << "    ✗ ERROR: Failed to create shader object" << std::endl;
        std::cerr << "      GL Error Code: " << glGetError() << std::endl;
        return 0;
    }
    
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    int success = 0;
    char infoLog[1024] = {0};
    
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        
        std::cerr << "    ✗ ERROR: Shader compilation failed" << std::endl;
        std::cerr << "      Info Log:\n" << infoLog << std::endl;
        std::cerr << "      GL Error: " << glGetError() << std::endl;
        
        glDeleteShader(shader);
        return 0;
    }
    
    std::cout << "    ✓ Shader compiled successfully (ID: " << shader << ")" << std::endl;
    return shader;
}

GLuint createShaderProgram_gl(const char *vertexSrc, const char *fragmentSrc) {
    std::cout << "Creating shader program..." << std::endl;
    
    if (glGetString(GL_VERSION) == nullptr) {
        std::cerr << "  ✗ ERROR: No OpenGL context for program creation" << std::endl;
        return 0;
    }
    
    GLuint vertexShader = compileShader_gl(vertexSrc, GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        std::cerr << "  ✗ Failed to compile vertex shader" << std::endl;
        return 0;
    }
    
    GLuint fragmentShader = compileShader_gl(fragmentSrc, GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        std::cerr << "  ✗ Failed to compile fragment shader" << std::endl;
        glDeleteShader(vertexShader);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    if (program == 0) {
        std::cerr << "  ✗ ERROR: Failed to create shader program" << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }
    
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    int success = 0;
    char infoLog[1024] = {0};
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if (!success) {
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "  ✗ ERROR: Shader program linking failed" << std::endl;
        std::cerr << "    Info Log:\n" << infoLog << std::endl;
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    std::cout << "✓ Shader program created successfully (ID: " << program << ")" << std::endl;
    return program;
}

// Quad vertices for card rendering (2D)
static const float QUAD_VERTICES_GL[] = {
    // position    // texture coords
    0.0f, 1.0f,   0.0f, 1.0f,
    1.0f, 1.0f,   1.0f, 1.0f,
    1.0f, 0.0f,   1.0f, 0.0f,
    0.0f, 0.0f,   0.0f, 0.0f
};

static const unsigned int QUAD_INDICES_GL[] = {
    0, 1, 2,
    2, 3, 0
};

void SolitaireGame::explodeCard_gl(AnimatedCard &card) {
    card.exploded = true;
    //playSound(GameSoundEvent::Firework);

    card.fragments.clear();

    const int grid_size = 4;
    const int fragment_width = current_card_width_ / grid_size;
    const int fragment_height = current_card_height_ / grid_size;

    for (int row = 0; row < grid_size; row++) {
        for (int col = 0; col < grid_size; col++) {
            CardFragment fragment;

            fragment.x = card.x + col * fragment_width;
            fragment.y = card.y + row * fragment_height;
            fragment.width = fragment_width;
            fragment.height = fragment_height;

            double center_x = card.x + current_card_width_ / 2;
            double center_y = card.y + current_card_height_ / 2;
            double fragment_center_x = fragment.x + fragment_width / 2;
            double fragment_center_y = fragment.y + fragment_height / 2;

            double dir_x = fragment_center_x - center_x;
            double dir_y = fragment_center_y - center_y;

            double magnitude = sqrt(dir_x * dir_x + dir_y * dir_y);
            if (magnitude > 0.001) {
                dir_x /= magnitude;
                dir_y /= magnitude;
            } else {
                double rand_angle = 2.0 * M_PI * (rand() % 1000) / 1000.0;
                dir_x = cos(rand_angle);
                dir_y = sin(rand_angle);
            }

            double speed = 12.0 + (rand() % 8);
            double upward_bias = -15.0 - (rand() % 10);

            fragment.velocity_x = dir_x * speed + (rand() % 10 - 5);
            fragment.velocity_y = dir_y * speed + upward_bias;

            fragment.rotation = card.rotation;
            fragment.rotation_velocity = (rand() % 60 - 30) / 5.0;
            fragment.surface = nullptr;
            fragment.active = true;
            
            // Store grid position in target_x and target_y for texture coordinate calculation
            // These fields are not used during explosion animation
            fragment.target_x = col / (double)grid_size;  // UV start X (0.0, 0.25, 0.5, 0.75)
            fragment.target_y = row / (double)grid_size;  // UV start Y
            
            // Store card info for drawing - encode as "card_suit_rank" in face_up (as bool, so just use flag)
            // Actually, we'll rely on finding the card through the animated_cards_ iteration

            card.fragments.push_back(fragment);
        }
    }
}

// ============================================================================
// Drawing Functions - OpenGL Version
// ============================================================================

void SolitaireGame::drawAnimatedCard_gl(const AnimatedCard &anim_card,
                                        GLuint shaderProgram,
                                        GLuint VAO) {
    if (!anim_card.active)
        return;

    glm::mat4 model = glm::mat4(1.0f);
    
    // With 0-1 vertex coordinates, center is at (0.5, 0.5) in local space
    model = glm::translate(model, glm::vec3(anim_card.x, anim_card.y, 0.0f));
    model = glm::translate(model, glm::vec3(current_card_width_ * 0.5f, 
                                             current_card_height_ * 0.5f, 0.0f));
    model = glm::rotate(model, static_cast<float>(anim_card.rotation), 
                        glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, glm::vec3(-current_card_width_ * 0.5f, 
                                             -current_card_height_ * 0.5f, 0.0f));
    model = glm::scale(model, glm::vec3(current_card_width_, current_card_height_, 1.0f));

    glUseProgram(shaderProgram);
    
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    
    // Bind the card texture
    GLint texLoc = glGetUniformLocation(shaderProgram, "cardTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    
    // Get the card image and bind it
    GLuint texture = cardBackTexture_gl_;
    auto card_image = deck_.getCardImage(anim_card.card);
    if (card_image && !card_image->data.empty()) {
        std::string card_key = std::to_string((int)anim_card.card.suit) + "_" + 
                               std::to_string((int)anim_card.card.rank);
        auto it = cardTextures_gl_.find(card_key);
        
        if (it != cardTextures_gl_.end()) {
            texture = it->second;
        } else {
            texture = loadTextureFromMemory(card_image->data);
            if (texture != 0) {
                cardTextures_gl_[card_key] = texture;
            }
        }
    }
    
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set alpha
    GLint alphaLoc = glGetUniformLocation(shaderProgram, "alpha");
    glUniform1f(alphaLoc, 1.0f);
    
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void SolitaireGame::drawCardFragment_gl(const CardFragment &fragment,
                                        const AnimatedCard &card,
                                        GLuint shaderProgram,
                                        GLuint VAO) {
    if (!fragment.active)
        return;

    // Get the card texture for this fragment
    GLuint cardTexture = cardBackTexture_gl_;
    auto card_image = deck_.getCardImage(card.card);
    if (card_image && !card_image->data.empty()) {
        std::string card_key = std::to_string((int)card.card.suit) + "_" + 
                               std::to_string((int)card.card.rank);
        auto it = cardTextures_gl_.find(card_key);
        
        if (it != cardTextures_gl_.end()) {
            cardTexture = it->second;
        } else {
            cardTexture = loadTextureFromMemory(card_image->data);
            if (cardTexture != 0) {
                cardTextures_gl_[card_key] = cardTexture;
            }
        }
    }
    
    glm::mat4 model = glm::mat4(1.0f);
    
    model = glm::translate(model, glm::vec3(fragment.x, fragment.y, 0.0f));
    model = glm::translate(model, glm::vec3(fragment.width * 0.5f, 
                                             fragment.height * 0.5f, 0.0f));
    model = glm::rotate(model, static_cast<float>(fragment.rotation), 
                        glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, glm::vec3(-fragment.width * 0.5f, 
                                             -fragment.height * 0.5f, 0.0f));
    model = glm::scale(model, glm::vec3(fragment.width, fragment.height, 1.0f));

    glUseProgram(shaderProgram);
    
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    
    // Bind the card front texture (not the back!)
    GLint texLoc = glGetUniformLocation(shaderProgram, "cardTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cardTexture);
    
    // Use the texture coordinates from the fragment (stored in target_x/target_y)
    // These are normalized coordinates for the grid position (0-0.25, 0.25-0.5, etc.)
    GLint texCoordLoc = glGetUniformLocation(shaderProgram, "texCoordOffset");
    if (texCoordLoc != -1) {
        glUniform2f(texCoordLoc, static_cast<float>(fragment.target_x), 
                                 static_cast<float>(fragment.target_y));
    }
    
    // Fragment of card is slightly transparent to show explosion effect
    GLint alphaLoc = glGetUniformLocation(shaderProgram, "alpha");
    glUniform1f(alphaLoc, 0.9f);
    
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void SolitaireGame::drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO) {
    for (const auto &anim_card : animated_cards_) {
        if (!anim_card.active)
            continue;

        if (!anim_card.exploded) {
            drawAnimatedCard_gl(anim_card, shaderProgram, VAO);
        } else {
            for (const auto &fragment : anim_card.fragments) {
                if (fragment.active) {
                    drawCardFragment_gl(fragment, anim_card, shaderProgram, VAO);
                }
            }
        }
    }
}

void SolitaireGame::drawDealAnimation_gl(GLuint shaderProgram, GLuint VAO) {
    for (const auto &anim_card : deal_cards_) {
        if (anim_card.active) {
            drawAnimatedCard_gl(anim_card, shaderProgram, VAO);
        }
    }
}

void SolitaireGame::drawFoundationAnimation_gl(GLuint shaderProgram, GLuint VAO) {
    if (foundation_move_animation_active_) {
        drawAnimatedCard_gl(foundation_move_card_, shaderProgram, VAO);
    }
}

void SolitaireGame::drawStockToWasteAnimation_gl(GLuint shaderProgram, GLuint VAO) {
    if (stock_to_waste_animation_active_) {
        drawAnimatedCard_gl(stock_to_waste_card_, shaderProgram, VAO);
    }
}

// ============================================================================
// OpenGL Drag and Drop Support - CRITICAL FIX
// ============================================================================

void SolitaireGame::drawDraggedCards_gl(GLuint shaderProgram, GLuint VAO) {
    // Draw cards being dragged  
    if (dragging_ && !drag_cards_.empty()) {
        int drag_x = static_cast<int>(drag_start_x_ - drag_offset_x_);
        int drag_y = static_cast<int>(drag_start_y_ - drag_offset_y_);
        
        for (size_t i = 0; i < drag_cards_.size(); i++) {
            drawCard_gl(drag_cards_[i], drag_x,
                       drag_y + static_cast<int>(i) * current_vert_spacing_, true);
        }
    }
}

// ============================================================================
// OpenGL Setup Functions
// ============================================================================

GLuint SolitaireGame::setupCardQuadVAO_gl() {
    std::cout << "\nSetting up card quad VAO..." << std::endl;
    
    if (!validateOpenGLContext_gl()) {
        std::cerr << "✗ Cannot setup VAO - no OpenGL context available" << std::endl;
        return 0;
    }
    
    if (!is_glew_initialized_) {
        std::cerr << "✗ Cannot setup VAO - GLEW not initialized" << std::endl;
        return 0;
    }
    
    static const float quadVertices[] = {
         0.0f,  0.0f,  0.0f, 0.0f,
         1.0f,  0.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
         0.0f,  1.0f,  0.0f, 1.0f
    };
    
    static const unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };
    
    GLuint VAO = 0, VBO = 0, EBO = 0;
    
    GLenum err = glGetError();
    while (err != GL_NO_ERROR) {
        std::cout << "  Clearing pre-existing GL error: " << err << std::endl;
        err = glGetError();
    }
    
    std::cout << "  Generating VAO..." << std::endl;
    glGenVertexArrays(1, &VAO);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "  ✗ glGenVertexArrays failed with error: " << err << std::endl;
        return 0;
    }
    if (VAO == 0) {
        std::cerr << "  ✗ glGenVertexArrays returned invalid VAO (0)" << std::endl;
        return 0;
    }
    std::cout << "    ✓ VAO generated (ID: " << VAO << ")" << std::endl;
    
    std::cout << "  Generating VBO..." << std::endl;
    glGenBuffers(1, &VBO);
    err = glGetError();
    if (err != GL_NO_ERROR || VBO == 0) {
        std::cerr << "  ✗ glGenBuffers(VBO) failed with error: " << err << std::endl;
        glDeleteVertexArrays(1, &VAO);
        return 0;
    }
    std::cout << "    ✓ VBO generated (ID: " << VBO << ")" << std::endl;
    
    std::cout << "  Generating EBO..." << std::endl;
    glGenBuffers(1, &EBO);
    err = glGetError();
    if (err != GL_NO_ERROR || EBO == 0) {
        std::cerr << "  ✗ glGenBuffers(EBO) failed with error: " << err << std::endl;
        glDeleteBuffers(1, &VBO);
        glDeleteVertexArrays(1, &VAO);
        return 0;
    }
    std::cout << "    ✓ EBO generated (ID: " << EBO << ")" << std::endl;
    
    std::cout << "  Configuring VAO..." << std::endl;
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "  ✗ OpenGL error during VAO configuration: " << err << std::endl;
        glDeleteBuffers(1, &EBO);
        glDeleteBuffers(1, &VBO);
        glDeleteVertexArrays(1, &VAO);
        return 0;
    }
    
    std::cout << "✓ Card quad VAO setup complete (VAO ID: " << VAO << ")" << std::endl;
    return VAO;
}

GLuint SolitaireGame::setupShaders_gl() {
    std::cout << "\nSetting up shaders..." << std::endl;
    
    if (!validateOpenGLContext_gl()) {
        std::cerr << "✗ Cannot setup shaders - no OpenGL context available" << std::endl;
        return 0;
    }
    
    GLuint program = createShaderProgram_gl(VERTEX_SHADER_GL, FRAGMENT_SHADER_GL);
    
    if (program == 0) {
        std::cerr << "✗ Failed to create shader program" << std::endl;
        return 0;
    }
    
    std::cout << "✓ Shaders setup complete" << std::endl;
    return program;
}

bool SolitaireGame::reloadCustomCardBackTexture_gl() {
    if (custom_back_path_.empty()) {
        std::cerr << "ERROR: No custom back path set" << std::endl;
        return false;
    }

    if (!validateOpenGLContext_gl()) {
        std::cerr << "ERROR: No OpenGL context available for custom back texture" << std::endl;
        return false;
    }

    try {
        // Read custom back image from disk
        std::ifstream file(custom_back_path_, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "ERROR: Failed to open custom back file: " << custom_back_path_ << std::endl;
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> imageData(size);
        if (!file.read(reinterpret_cast<char*>(imageData.data()), size)) {
            std::cerr << "ERROR: Failed to read custom back file" << std::endl;
            return false;
        }
        file.close();

        // Delete old texture if it exists
        if (cardBackTexture_gl_ != 0) {
            glDeleteTextures(1, &cardBackTexture_gl_);
            cardBackTexture_gl_ = 0;
        }

        // Load custom back image and create new texture
        std::cout << "Loading custom card back texture from: " << custom_back_path_ << std::endl;
        cardBackTexture_gl_ = loadTextureFromMemory(imageData);
        
        if (cardBackTexture_gl_ != 0) {
            std::cout << "✓ Custom card back texture loaded successfully (Texture ID: " 
                      << cardBackTexture_gl_ << ")" << std::endl;
            return true;
        } else {
            std::cerr << "ERROR: Failed to create texture from custom back image" << std::endl;
            return false;
        }

    } catch (const std::exception &e) {
        std::cerr << "EXCEPTION: Failed to reload custom card back texture: " << e.what() << std::endl;
        return false;
    }
}

bool SolitaireGame::initializeCardTextures_gl() {
    std::cout << "\nInitializing card textures..." << std::endl;
    
    if (!validateOpenGLContext_gl()) {
        std::cerr << "✗ Cannot initialize textures - no OpenGL context available" << std::endl;
        return false;
    }
    
    try {
        // CRITICAL FIX: Load the actual card back image from the deck
        if (auto back_img = deck_.getCardBackImage()) {
            if (!back_img->data.empty()) {
                std::cout << "  Loading actual card back image from deck..." << std::endl;
                cardBackTexture_gl_ = loadTextureFromMemory(back_img->data);
                if (cardBackTexture_gl_ != 0) {
                    std::cout << "✓ Card back texture loaded successfully (Texture ID: " 
                              << cardBackTexture_gl_ << ")" << std::endl;
                    return true;
                } else {
                    std::cerr << "  ⚠ Failed to load card back from memory, creating fallback..." << std::endl;
                }
            }
        }
        
        // Fallback: Create a placeholder texture if real card back failed to load
        const int TEX_WIDTH = 32;
        const int TEX_HEIGHT = 48;
        const int TEX_CHANNELS = 4;
        
        std::cout << "  Creating fallback placeholder texture (" << TEX_WIDTH << "x" << TEX_HEIGHT << ")..." << std::endl;
        
        // Create a nice gray placeholder instead of pure white
        unsigned char textureData[TEX_WIDTH * TEX_HEIGHT * TEX_CHANNELS];
        memset(textureData, 200, sizeof(textureData)); // Gray color instead of white
        
        GLuint texture = 0;
        glGenTextures(1, &texture);
        
        if (texture == 0) {
            std::cerr << "  ✗ ERROR: Failed to generate texture object" << std::endl;
            std::cerr << "    GL Error: " << glGetError() << std::endl;
            return false;
        }
        std::cout << "    ✓ Texture object created (ID: " << texture << ")" << std::endl;
        
        glBindTexture(GL_TEXTURE_2D, texture);
        
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "  ✗ ERROR: Failed to bind texture: " << err << std::endl;
            glDeleteTextures(1, &texture);
            return false;
        }
        
        std::cout << "  Setting texture parameters..." << std::endl;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        std::cout << "  Uploading texture data..." << std::endl;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_WIDTH, TEX_HEIGHT, 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, textureData);
        
        err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "  ✗ ERROR: Failed to upload texture data: " << err << std::endl;
            glDeleteTextures(1, &texture);
            return false;
        }
        
        cardBackTexture_gl_ = texture;
        std::cout << "✓ Card textures initialized successfully (Texture ID: " << texture << ")" << std::endl;
        return true;
        
    } catch (const std::exception &e) {
        std::cerr << "✗ EXCEPTION: Failed to initialize card textures" << std::endl;
        std::cerr << "  What: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "✗ UNKNOWN EXCEPTION: Failed to initialize card textures" << std::endl;
        return false;
    }
}

bool SolitaireGame::initializeRenderingEngine_gl() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "INITIALIZING RENDERING ENGINE" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    
    if (rendering_engine_ != RenderingEngine::OPENGL) {
        std::cout << "✓ Using Cairo rendering (CPU-based)" << std::endl;
        cairo_initialized_ = true;
        return true;
    }
    
    std::cout << "Initializing OpenGL rendering engine..." << std::endl;
    std::cout << "\n[STEP 1/5] Validating OpenGL context..." << std::endl;
    if (!validateOpenGLContext_gl()) {
        std::cerr << "✗ FATAL: OpenGL context validation failed" << std::endl;
        std::cerr << "Falling back to Cairo mode" << std::endl;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ Context validated" << std::endl;
    
    std::cout << "\n[STEP 2/5] Initializing GLEW..." << std::endl;
    if (!initializeGLEW_gl()) {
        std::cerr << "✗ FATAL: GLEW initialization failed" << std::endl;
        std::cerr << "Falling back to Cairo mode" << std::endl;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ GLEW initialized" << std::endl;
    
    std::cout << "\n[STEP 3/5] Checking GPU capabilities..." << std::endl;
    if (!checkOpenGLCapabilities_gl()) {
        std::cerr << "✗ FATAL: GPU does not meet minimum requirements (OpenGL 3.3+)" << std::endl;
        std::cerr << "Falling back to Cairo mode" << std::endl;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ GPU capabilities verified" << std::endl;
    
    std::cout << "\n[STEP 4/5] Compiling shaders..." << std::endl;
    cardShaderProgram_gl_ = setupShaders_gl();
    if (cardShaderProgram_gl_ == 0) {
        std::cerr << "✗ FATAL: Shader compilation failed" << std::endl;
        glDeleteProgram(cardShaderProgram_gl_);
        cardShaderProgram_gl_ = 0;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ Shaders compiled and linked" << std::endl;
    
    std::cout << "\n[STEP 5/5] Setting up vertex arrays and textures..." << std::endl;
    cardQuadVAO_gl_ = setupCardQuadVAO_gl();
    if (cardQuadVAO_gl_ == 0) {
        std::cerr << "✗ FATAL: VAO setup failed" << std::endl;
        cleanupOpenGLResources_gl();
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ VAO created" << std::endl;
    
    if (!initializeCardTextures_gl()) {
        std::cerr << "✗ FATAL: Texture initialization failed" << std::endl;
        cleanupOpenGLResources_gl();
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ Textures initialized" << std::endl;
    
    opengl_initialized_ = true;
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "✓ OPENGL RENDERING ENGINE READY" << std::endl;
    std::cout << std::string(70, '=') << "\n" << std::endl;
    
    return true;
}

// ============================================================================
// GL DRAWING FUNCTIONS FOR GAME PILES
// ============================================================================

GLuint SolitaireGame::loadTextureFromMemory(const std::vector<unsigned char> &data) {
    if (data.empty()) return 0;
    
    // Decode PNG from memory
    int width, height, channels;
    unsigned char *pixels = stbi_load_from_memory(
        data.data(), data.size(), 
        &width, &height, &channels, STBI_rgb_alpha
    );
    
    if (!pixels) {
        fprintf(stderr, "[GL] ERROR: Failed to decode PNG from memory\n");
        return 0;
    }
    
    // Create texture
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    stbi_image_free(pixels);
    return texture;
}

void SolitaireGame::drawCard_gl(const cardlib::Card &card, int x, int y, bool face_up) {
    static int count = 0;
    if (count++ == 0) fprintf(stderr, "[GL] DRAWING CARDS NOW\n");
    
    if (cardShaderProgram_gl_ == 0 || cardQuadVAO_gl_ == 0) {
        return;
    }
    
    // Default to card back texture
    GLuint texture = cardBackTexture_gl_;
    
    if (face_up) {
        // Try to get the face-up card image
        auto card_image = deck_.getCardImage(card);
        if (card_image && !card_image->data.empty()) {
            std::string card_key = std::to_string((int)card.suit) + "_" + std::to_string((int)card.rank);
            auto it = cardTextures_gl_.find(card_key);
            
            if (it != cardTextures_gl_.end()) {
                // Use cached texture
                texture = it->second;
            } else {
                // Load texture and cache it
                texture = loadTextureFromMemory(card_image->data);
                if (texture != 0) {
                    cardTextures_gl_[card_key] = texture;
                } else {
                    // Fallback to card back if loading failed
                    texture = cardBackTexture_gl_;
                }
            }
        }
    }
    // For face_down cards, use the default cardBackTexture_gl_ already set above
    
    // Draw card at position
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3((float)x, (float)y, 0.0f));
    model = glm::scale(model, glm::vec3((float)current_card_width_, (float)current_card_height_, 1.0f));
    
    GLint modelLoc = glGetUniformLocation(cardShaderProgram_gl_, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    
    // Set alpha uniform to fully opaque
    GLint alphaLoc = glGetUniformLocation(cardShaderProgram_gl_, "alpha");
    glUniform1f(alphaLoc, 1.0f);
    
    // Set texture uniform
    GLint texLoc = glGetUniformLocation(cardShaderProgram_gl_, "cardTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    
    if (texture != 0) {
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    
    glBindVertexArray(cardQuadVAO_gl_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

// Draw foundation pile during win animation
void SolitaireGame::drawFoundationDuringWinAnimation_gl(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y) {
  // Only draw the topmost non-animated card
  for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
    if (!animated_foundation_cards_[pile_index][j]) {
      drawCard_gl(pile[j], x, y, true);
      break;
    }
  }
}

// Draw foundation pile during normal gameplay
void SolitaireGame::drawNormalFoundationPile_gl(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y) {
  // Check if the top card is being dragged from foundation
  bool top_card_dragging =
      (dragging_ && drag_source_pile_ == pile_index + 2 &&
       !pile.empty() && drag_cards_.size() == 1 &&
       drag_cards_[0].suit == pile.back().suit &&
       drag_cards_[0].rank == pile.back().rank);

  if (!top_card_dragging) {
    const auto &top_card = pile.back();
    drawCard_gl(top_card, x, y, true);
  } else if (pile.size() > 1) {
    // Draw the second-to-top card
    const auto &second_card = pile[pile.size() - 2];
    drawCard_gl(second_card, x, y, true);
  }
}

// Helper function to draw empty pile placeholders (light gray rectangle like Cairo)
void SolitaireGame::drawEmptyPile_gl(int x, int y) {
    // Draw light gray rectangle placeholder for empty pile
    // This matches Cairo's appearance exactly: RGBA(0.85, 0.85, 0.85, 0.5)
    
    // Create light gray texture on first use (static, cached)
    static GLuint emptyPileTexture = 0;
    
    if (emptyPileTexture == 0) {
        const int WIDTH = 32;
        const int HEIGHT = 48;
        
        unsigned char data[WIDTH * HEIGHT * 4];
        for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
            data[i] = (unsigned char)(0.85f * 255);     // R: 0.85
            data[i + 1] = (unsigned char)(0.85f * 255); // G: 0.85
            data[i + 2] = (unsigned char)(0.85f * 255); // B: 0.85
            data[i + 3] = (unsigned char)(0.5f * 255);  // A: 0.5 (50% opacity)
        }
        
        glGenTextures(1, &emptyPileTexture);
        glBindTexture(GL_TEXTURE_2D, emptyPileTexture);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3((float)x, (float)y, 0.0f));
    model = glm::scale(model, glm::vec3((float)current_card_width_, (float)current_card_height_, 1.0f));
    
    GLint modelLoc = glGetUniformLocation(cardShaderProgram_gl_, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    
    // Set full alpha (transparency handled by texture)
    GLint alphaLoc = glGetUniformLocation(cardShaderProgram_gl_, "alpha");
    glUniform1f(alphaLoc, 1.0f);
    
    // Draw with light gray placeholder texture
    GLint texLoc = glGetUniformLocation(cardShaderProgram_gl_, "cardTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    
    glBindTexture(GL_TEXTURE_2D, emptyPileTexture);
    glBindVertexArray(cardQuadVAO_gl_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

// Helper function to draw a highlighted rectangle around selected cards
void SolitaireGame::highlightSelectedCard_gl() {
  if (!keyboard_navigation_active_ || selected_pile_ == -1) {
    return;
  }

  int x = 0, y = 0;

  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + static_cast<int>(foundation_.size()) - 1;
  int first_tableau_index = max_foundation_index + 1;

  // Validate keyboard selection
  if (keyboard_selection_active_) {
    bool invalid_source = false;
    
    if (source_pile_ < 0) {
      invalid_source = true;
    } else if (source_pile_ >= first_tableau_index) {
      int tableau_idx = source_pile_ - first_tableau_index;
      if (tableau_idx < 0 || tableau_idx >= static_cast<int>(tableau_.size())) {
        invalid_source = true;
      }
    } else if (source_pile_ == 1 && waste_.empty()) {
      invalid_source = true;
    } else if (source_pile_ >= 2 && source_pile_ <= max_foundation_index) {
      int foundation_idx = source_pile_ - 2;
      if (foundation_idx < 0 || foundation_idx >= static_cast<int>(foundation_.size())) {
        invalid_source = true;
      }
    }
    
    if (invalid_source) {
      // Invalid source pile, reset selection state
      keyboard_selection_active_ = false;
      source_pile_ = -1;
      source_card_idx_ = -1;
      return;
    }
  }

  // Determine position based on pile type (matching Cairo logic)
  if (selected_pile_ == 0) {
    // Stock pile
    x = current_card_spacing_;
    y = current_card_spacing_;
  } else if (selected_pile_ == 1) {
    // Waste pile
    x = 2 * current_card_spacing_ + current_card_width_;
    y = current_card_spacing_;
  } else if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
    // Foundation piles - match the exact calculation from drawFoundationPiles()
    int foundation_idx = selected_pile_ - 2;
    
    // Make sure foundation_idx is valid
    if (foundation_idx >= 0 && foundation_idx < static_cast<int>(foundation_.size())) {
      x = 3 * (current_card_width_ + current_card_spacing_) + 
          foundation_idx * (current_card_width_ + current_card_spacing_);
      y = current_card_spacing_;
    }
  } else if (selected_pile_ >= first_tableau_index) {
    // Tableau piles
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
      x = current_card_spacing_ +
          tableau_idx * (current_card_width_ + current_card_spacing_);
      
      const auto &tableau_pile = tableau_[tableau_idx];
      if (tableau_pile.empty()) {
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
      } else if (selected_card_idx_ == -1 || selected_card_idx_ >= static_cast<int>(tableau_pile.size())) {
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
            (tableau_pile.size() - 1) * current_vert_spacing_;
      } else {
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
            selected_card_idx_ * current_vert_spacing_;
      }
    }
  }

  // Choose highlight color based on selection state (matching Cairo colors)
  float r, g, b, a;
  if (keyboard_selection_active_ && source_pile_ == selected_pile_ &&
      (source_card_idx_ == selected_card_idx_ || selected_card_idx_ == -1)) {
    // Source card is highlighted in semi-transparent blue
    r = 0.0f; g = 0.5f; b = 1.0f; a = 0.5f;
  } else {
    // Regular selection is highlighted in semi-transparent yellow
    r = 1.0f; g = 1.0f; b = 0.0f; a = 0.5f;
  }
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Draw filled rectangle outline (4 line segments forming a box outline)
    // Create the 4 corners of the rectangle
    float positions[] = {
        (float)(x - 2), (float)(y - 2), 0.0f,
        (float)(x + current_card_width_ + 2), (float)(y - 2), 0.0f,
        (float)(x + current_card_width_ + 2), (float)(y + current_card_height_ + 2), 0.0f,
        (float)(x - 2), (float)(y + current_card_height_ + 2), 0.0f
    };
    
    // Create color array for all 4 corners (all same color)
    float colors[] = {
        r, g, b, a,
        r, g, b, a,
        r, g, b, a,
        r, g, b, a
    };
    
    // Create VAO/VBO for the rectangle
    GLuint VAO = 0, VBO_pos = 0, VBO_color = 0;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO_pos);
    glGenBuffers(1, &VBO_color);
    
    glBindVertexArray(VAO);
    
    // Position buffer
    glBindBuffer(GL_ARRAY_BUFFER, VBO_pos);
    glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Color buffer
    glBindBuffer(GL_ARRAY_BUFFER, VBO_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    
    // Get window dimensions for projection
    GtkAllocation allocation;
    gtk_widget_get_allocation(gl_area_, &allocation);
    
    // Use simple fixed pipeline approach: disable texturing and use color directly
    // Save current state
    GLint oldProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    
    // Create minimal shader for colored geometry
    static GLuint colorShaderProgram = 0;
    if (colorShaderProgram == 0) {
        const char *vertShader = R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec4 color;
            
            uniform mat4 projection;
            uniform mat4 view;
            
            out VS_OUT {
                vec4 color;
            } vs_out;
            
            void main() {
                gl_Position = projection * view * vec4(position, 1.0);
                vs_out.color = color;
            }
        )";
        
        const char *fragShader = R"(
            #version 330 core
            in VS_OUT {
                vec4 color;
            } fs_in;
            
            out vec4 FragColor;
            
            void main() {
                FragColor = fs_in.color;
            }
        )";
        
        // Compile vertex shader
        GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vShader, 1, &vertShader, NULL);
        glCompileShader(vShader);
        
        // Compile fragment shader
        GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fShader, 1, &fragShader, NULL);
        glCompileShader(fShader);
        
        // Create program
        colorShaderProgram = glCreateProgram();
        glAttachShader(colorShaderProgram, vShader);
        glAttachShader(colorShaderProgram, fShader);
        glLinkProgram(colorShaderProgram);
        
        glDeleteShader(vShader);
        glDeleteShader(fShader);
    }
    
    // Use the color shader program
    glUseProgram(colorShaderProgram);
    
    // Set up matrices
    glm::mat4 projection = glm::ortho(0.0f, (float)allocation.width, 
                                      (float)allocation.height, 0.0f, -1.0f, 1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    
    GLint projLoc = glGetUniformLocation(colorShaderProgram, "projection");
    GLint viewLoc = glGetUniformLocation(colorShaderProgram, "view");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    
    // Draw the rectangle outline as line loop with increased line width
    glLineWidth(3.0f);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glLineWidth(1.0f);
    
    glDeleteBuffers(1, &VBO_pos);
    glDeleteBuffers(1, &VBO_color);
    glDeleteVertexArrays(1, &VAO);
    
    // If we have a card selected for movement, also highlight all cards below it in tableau
    if (keyboard_selection_active_ && source_pile_ >= first_tableau_index && source_card_idx_ >= 0) {
        int tableau_idx = source_pile_ - first_tableau_index;
        if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
            const auto &tableau_pile = tableau_[tableau_idx];
            
            if (!tableau_pile.empty() && source_card_idx_ < static_cast<int>(tableau_pile.size())) {
                int x2 = current_card_spacing_ +
                    tableau_idx * (current_card_width_ + current_card_spacing_);
                int y2 = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
                    source_card_idx_ * current_vert_spacing_;
                
                int stack_height =
                    (tableau_pile.size() - source_card_idx_ - 1) * current_vert_spacing_ +
                    current_card_height_;
                
                if (stack_height > 0) {
                    // Lighter alpha for multi-card selection (still blue)
                    float positions2[] = {
                        (float)(x2 - 2), (float)(y2 - 2), 0.0f,
                        (float)(x2 + current_card_width_ + 2), (float)(y2 - 2), 0.0f,
                        (float)(x2 + current_card_width_ + 2), (float)(y2 + stack_height + 2), 0.0f,
                        (float)(x2 - 2), (float)(y2 + stack_height + 2), 0.0f
                    };
                    
                    float colors2[] = {
                        r, g, b, 0.3f,  // Lighter blue for stack
                        r, g, b, 0.3f,
                        r, g, b, 0.3f,
                        r, g, b, 0.3f
                    };
                    
                    GLuint VAO2 = 0, VBO2_pos = 0, VBO2_color = 0;
                    glGenVertexArrays(1, &VAO2);
                    glGenBuffers(1, &VBO2_pos);
                    glGenBuffers(1, &VBO2_color);
                    
                    glBindVertexArray(VAO2);
                    
                    glBindBuffer(GL_ARRAY_BUFFER, VBO2_pos);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(positions2), positions2, GL_STATIC_DRAW);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(0);
                    
                    glBindBuffer(GL_ARRAY_BUFFER, VBO2_color);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(colors2), colors2, GL_STATIC_DRAW);
                    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(1);
                    
                    // Use the same color shader program as the main highlight
                    glUseProgram(colorShaderProgram);
                    
                    glLineWidth(3.0f);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);
                    glLineWidth(1.0f);
                    
                    glDeleteBuffers(1, &VBO2_pos);
                    glDeleteBuffers(1, &VBO2_color);
                    glDeleteVertexArrays(1, &VAO2);
                }
            }
        }
    }
    
    // Disable blending
    glDisable(GL_BLEND);
}

void SolitaireGame::renderFrame_gl() {
    if (!opengl_initialized_) {
        glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }
    
    if (rendering_engine_ != RenderingEngine::OPENGL || !opengl_initialized_) {
        return;
    }
    
    // CRITICAL FIX: Get actual window dimensions instead of hardcoding
    GtkAllocation allocation;
    gtk_widget_get_allocation(gl_area_, &allocation);
    
    static int prev_width = -1, prev_height = -1;
    static bool first = true;
    if (first || allocation.width != prev_width || allocation.height != prev_height) {
        fprintf(stderr, "[GL] Window dimensions: %d x %d\n", allocation.width, allocation.height);
        fprintf(stderr, "[GL] Card dimensions: width=%d, height=%d, spacing=%d, vert_spacing=%d\n",
                current_card_width_, current_card_height_, current_card_spacing_, current_vert_spacing_);
        prev_width = allocation.width;
        prev_height = allocation.height;
        first = false;
    }
    
    // Clear screen
    glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // CRITICAL FIX: Set viewport to match actual window size
    glViewport(0, 0, allocation.width, allocation.height);
    
    // Setup matrices
    glUseProgram(cardShaderProgram_gl_);
    
    // CRITICAL FIX: Use actual window dimensions instead of hardcoded 1920x1080
    // This is the key fix for card sizing and positioning!
    glm::mat4 projection = glm::ortho(0.0f, (float)allocation.width, 
                                      (float)allocation.height, 0.0f, -1.0f, 1.0f);
    GLint projLoc = glGetUniformLocation(cardShaderProgram_gl_, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    
    glm::mat4 view = glm::mat4(1.0f);
    GLint viewLoc = glGetUniformLocation(cardShaderProgram_gl_, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    
    // Enable blending for transparency (used for empty pile indicators)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Draw all game piles
    drawStockPile_gl();
    drawFoundationPiles_gl();
    drawTableauPiles_gl();
    
    // Draw animations if active
    if (win_animation_active_) {
        drawWinAnimation_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    }
    if (deal_animation_active_) {
        drawDealAnimation_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    }
    if (foundation_move_animation_active_) {
        drawFoundationAnimation_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    }
    if (stock_to_waste_animation_active_) {
        drawStockToWasteAnimation_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    }
    
    // Draw dragged cards overlay - CRITICAL FIX FOR DRAG VISUALIZATION
    drawDraggedCards_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    
    // Draw keyboard navigation highlight if active (matching Cairo behavior)
    if (keyboard_navigation_active_ && !dragging_ &&
        !deal_animation_active_ && !win_animation_active_ &&
        !foundation_move_animation_active_ &&
        !stock_to_waste_animation_active_) {
        highlightSelectedCard_gl();
    }
    
    // Disable blending after drawing
    glDisable(GL_BLEND);
}

// ============================================================================
// Auto-Finish Animation - OpenGL 3.4 Version
// ============================================================================

gboolean SolitaireGame::onAutoFinishTick_gl(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->processNextAutoFinishMove_gl();
    return game->auto_finish_active_ ? TRUE : FALSE;
}

void SolitaireGame::processNextAutoFinishMove_gl() {
    // Placeholder for auto-finish logic
}

// ============================================================================
// OPENGL DRAWING FUNCTIONS - Mirror Cairo logic with GL calls
// ============================================================================

void SolitaireGame::drawStockPile_gl() {
  int x = current_card_spacing_;
  int y = current_card_spacing_;
  
  if (stock_.empty()) {
    drawEmptyPile_gl(x, y);
  } else {
    drawCard_gl(cardlib::Card(cardlib::Suit::HEARTS, cardlib::Rank::KING), x, y, false);
  }
}

void SolitaireGame::drawFoundationPiles_gl() {
  int foundation_x = 2 * current_card_spacing_ + current_card_width_;
  int foundation_y = current_card_spacing_;
  int completed_sequences = foundation_[0].size();

  if (win_animation_active_) {
    for (int pile = 0; pile < 8; pile++) {
      cardlib::Suit suit;
      switch (pile % 4) {
        case 0: suit = cardlib::Suit::HEARTS; break;
        case 1: suit = cardlib::Suit::DIAMONDS; break;
        case 2: suit = cardlib::Suit::CLUBS; break;
        case 3: suit = cardlib::Suit::SPADES; break;
      }
      
      cardlib::Rank rank = cardlib::Rank::KING;
      for (int i = 0; i < 13; i++) {
        if (pile < static_cast<int>(animated_foundation_cards_.size()) && 
            i < static_cast<int>(animated_foundation_cards_[pile].size()) &&
            !animated_foundation_cards_[pile][i]) {
          rank = static_cast<cardlib::Rank>(13 - i);
          break;
        }
      }
      
      cardlib::Card card(suit, rank);
      drawCard_gl(card, foundation_x, foundation_y, true);
      foundation_x += current_card_width_ + current_card_spacing_;
    }
  } else {
    for (int i = 0; i < 8; i++) {
      if (sequence_animation_active_ && i == completed_sequences) {
        bool foundation_card_found = false;
        for (int j = sequence_cards_.size() - 1; j >= 0; j--) {
          if (!sequence_cards_[j].active && 
              sequence_cards_[j].x == sequence_cards_[j].target_x && 
              sequence_cards_[j].y == sequence_cards_[j].target_y) {
            const cardlib::Card &card = sequence_cards_[j].card;
            drawCard_gl(card, foundation_x, foundation_y, true);
            foundation_card_found = true;
            break;
          }
        }
        
        if (!foundation_card_found) {
          drawEmptyPile_gl(foundation_x, foundation_y);
        }
      } else if (i < completed_sequences) {
        const cardlib::Card &card = foundation_[0][i];
        drawCard_gl(card, foundation_x, foundation_y, true);
      } else {
        drawEmptyPile_gl(foundation_x, foundation_y);
      }
      
      foundation_x += current_card_width_ + current_card_spacing_;
    }
  }
}

void SolitaireGame::drawTableauPiles_gl() {
  const int tableau_base_y = current_card_spacing_ +
                           current_card_height_ +
                           current_vert_spacing_;

  for (size_t i = 0; i < tableau_.size(); i++) {
    int x = current_card_spacing_ +
          i * (current_card_width_ + current_card_spacing_);
    const auto &pile = tableau_[i];

    if (pile.empty()) {
      drawEmptyPile_gl(x, tableau_base_y);
    }

    if (deal_animation_active_) {
      drawTableauPileDuringAnimationGL(i, x, tableau_base_y);
    } else {
      drawNormalTableauPileGL(i, x, tableau_base_y);
    }
  }
}

void SolitaireGame::drawTableauPileDuringAnimationGL(size_t pile_index, int x, int tableau_base_y) {
  const auto &pile = tableau_[pile_index];
  
  int cards_in_this_pile = (pile_index < 6) ? 6 : 5;
  int total_cards_before_this_pile = 0;
  for (int p = 0; p < pile_index; p++) {
    total_cards_before_this_pile += (p < 6) ? 6 : 5;
  }
  
  int cards_to_draw = std::min(
      static_cast<int>(pile.size()),
      std::max(0, cards_dealt_ - total_cards_before_this_pile));
  
  for (int j = 0; j < cards_to_draw; j++) {
    bool is_animating = false;
    for (const auto &anim_card : deal_cards_) {
      if (anim_card.active) {
        if (anim_card.target_pile_index == static_cast<int>(pile_index) && 
            anim_card.target_card_index == j) {
          is_animating = true;
          break;
        }
      }
    }
    
    if (!is_animating) {
      int current_y = tableau_base_y + j * current_vert_spacing_;
      drawCard_gl(pile[j].card, x, current_y, pile[j].face_up);
    }
  }
}

void SolitaireGame::drawNormalTableauPileGL(size_t pile_index, int x, int tableau_base_y) {
  const auto &pile = tableau_[pile_index];
  
  for (size_t j = 0; j < pile.size(); j++) {
    if (dragging_ && drag_source_pile_ >= 6 &&
        drag_source_pile_ - 6 == static_cast<int>(pile_index) &&
        j >= static_cast<size_t>(pile.size() - drag_cards_.size())) {
      continue;
    }
    
    bool skip_for_animation = false;
    if (sequence_animation_active_ && sequence_tableau_index_ == static_cast<int>(pile_index)) {
      for (size_t anim_idx = 0; anim_idx < sequence_cards_.size(); anim_idx++) {
        if (sequence_cards_[anim_idx].active) {
          for (size_t pos_idx = 0; pos_idx < anim_idx; pos_idx++) {
            int position = sequence_card_positions_[pos_idx] - pos_idx;
            if (j == static_cast<size_t>(position)) {
              skip_for_animation = true;
              break;
            }
          }
          break;
        }
      }
    }
    
    if (skip_for_animation) {
      continue;
    }
    
    int current_y = tableau_base_y + j * current_vert_spacing_;
    const auto &tableau_card = pile[j];
    drawCard_gl(tableau_card.card, x, current_y, tableau_card.face_up);
  }
}

void SolitaireGame::cleanupOpenGLResources_gl() {
    if (cardShaderProgram_gl_ != 0) {
        glDeleteProgram(cardShaderProgram_gl_);
        cardShaderProgram_gl_ = 0;
    }
    
    if (cardQuadVAO_gl_ != 0) {
        glDeleteVertexArrays(1, &cardQuadVAO_gl_);
        cardQuadVAO_gl_ = 0;
    }
    
    if (cardBackTexture_gl_ != 0) {
        glDeleteTextures(1, &cardBackTexture_gl_);
        cardBackTexture_gl_ = 0;
    }
    
    for (auto &pair : cardTextures_gl_) {
        glDeleteTextures(1, &pair.second);
    }
    cardTextures_gl_.clear();
    
    std::cout << "OpenGL resources cleaned up" << std::endl;
}
