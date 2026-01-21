#include "solitaire.h"
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

bool SolitaireGame::validateOpenGLContext() {
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

bool SolitaireGame::initializeGLEW() {
    if (is_glew_initialized_) {
        std::cout << "✓ GLEW Already Initialized" << std::endl;
        return true;
    }
    
    if (!validateOpenGLContext()) {
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

bool SolitaireGame::checkOpenGLCapabilities() {
    std::cout << "\nChecking OpenGL Capabilities..." << std::endl;
    
    if (!validateOpenGLContext()) {
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
    
    logOpenGLInfo();
    return true;
}

void SolitaireGame::logOpenGLInfo() {
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

// ============================================================================
// Win Animation Functions - OpenGL Version
// ============================================================================

void SolitaireGame::updateWinAnimation_gl() {
    if (!win_animation_active_)
        return;

    launch_timer_ += ANIMATION_INTERVAL;
    if (launch_timer_ >= 100) {
        launch_timer_ = 0;
        if (rand() % 100 < 10) {
            for (int i = 0; i < 4; i++) {
                launchNextCard_gl();
                if (cards_launched_ >= 52)
                    break;
            }
        } else {
            launchNextCard_gl();
        }
    }

    bool all_cards_finished = true;
    
    int window_width = current_card_width_ * 10;
    int window_height = current_card_height_ * 10;

    const double explosion_min = window_height * EXPLOSION_THRESHOLD_MIN;
    const double explosion_max = window_height * EXPLOSION_THRESHOLD_MAX;

    for (auto &card : animated_cards_) {
        if (!card.active)
            continue;

        if (!card.exploded) {
            card.x += card.velocity_x;
            card.y += card.velocity_y;
            card.velocity_y += GRAVITY;

            card.rotation += card.rotation_velocity;

            if (card.y > explosion_min && card.y < explosion_max &&
                (rand() % 100 < 5)) {
                explodeCard_gl(card);
            }

            if (card.x < -current_card_width_ || card.x > window_width ||
                card.y > window_height + current_card_height_) {
                card.active = false;
            } else {
                all_cards_finished = false;
            }
        } else {
            updateCardFragments_gl(card);

            bool all_fragments_inactive = true;
            for (const auto &fragment : card.fragments) {
                if (fragment.active) {
                    all_fragments_inactive = false;
                    all_cards_finished = false;
                    break;
                }
            }

            if (all_fragments_inactive) {
                card.active = false;
            }
        }
    }

    if (all_cards_finished) {
        for (size_t i = 0; i < animated_foundation_cards_.size(); i++) {
            std::fill(animated_foundation_cards_[i].begin(), 
                      animated_foundation_cards_[i].end(), false);
        }
        cards_launched_ = 0;
    }

    refreshDisplay();
}

void SolitaireGame::startWinAnimation_gl() {
    if (win_animation_active_)
        return;

    resetKeyboardNavigation();
    playSound(GameSoundEvent::WinGame);

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, NULL);

    GtkWidget *message_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Congratulations! You've won!\n\n"
                                      "Click or press any key to stop the celebration "
                                      "and start a new game");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);
    gtk_widget_set_margin_top(label, 10);
    gtk_widget_set_margin_bottom(label, 10);

    gtk_container_add(GTK_CONTAINER(message_area), label);
    gtk_widget_show(label);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    win_animation_active_ = true;
    cards_launched_ = 0;
    launch_timer_ = 0;
    animated_cards_.clear();

    if (current_game_mode_ != GameMode::STANDARD_KLONDIKE) {
        std::vector<std::vector<cardlib::Card>> original_foundation = foundation_;
        
        foundation_.clear();
        foundation_.resize(4);
        
        for (int suit = 0; suit < 4; suit++) {
            foundation_[suit].clear();
            for (int rank = static_cast<int>(cardlib::Rank::ACE);
                 rank <= static_cast<int>(cardlib::Rank::KING);
                 rank++) {
                cardlib::Card card(static_cast<cardlib::Suit>(suit),
                                   static_cast<cardlib::Rank>(rank));
                foundation_[suit].push_back(card);
            }
        }
    }

    animated_foundation_cards_.clear();
    animated_foundation_cards_.resize(4);
    for (size_t i = 0; i < 4; i++) {
        animated_foundation_cards_[i].resize(13, false);
    }

    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onAnimationTick_gl, this);
}

void SolitaireGame::stopWinAnimation_gl() {
    if (!win_animation_active_)
        return;

    win_animation_active_ = false;

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    for (auto &card : animated_cards_) {
        card.fragments.clear();
    }

    animated_cards_.clear();
    animated_foundation_cards_.clear();
    cards_launched_ = 0;
    launch_timer_ = 0;

    initializeGame();
    refreshDisplay();
}

gboolean SolitaireGame::onAnimationTick_gl(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->updateWinAnimation_gl();
    
    // Force redraw after animation update
    if (game->gl_area_) {
        gtk_widget_queue_draw(game->gl_area_);
    }
    
    return game->win_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::launchNextCard_gl() {
    if (cards_launched_ >= 52)
        return;

    std::vector<int> valid_piles;

    for (size_t pile_index = 0; pile_index < foundation_.size(); pile_index++) {
        if (foundation_[pile_index].empty())
            continue;

        for (int card_index = static_cast<int>(foundation_[pile_index].size()) - 1;
             card_index >= 0; card_index--) {
            if (card_index < static_cast<int>(animated_foundation_cards_[pile_index].size()) &&
                !animated_foundation_cards_[pile_index][card_index]) {
                valid_piles.push_back(pile_index);
                break;
            }
        }
    }

    if (valid_piles.empty())
        return;

    int random_pile_index = valid_piles[rand() % valid_piles.size()];

    int card_index = -1;
    for (int i = static_cast<int>(foundation_[random_pile_index].size()) - 1; i >= 0; i--) {
        if (i < static_cast<int>(animated_foundation_cards_[random_pile_index].size()) &&
            !animated_foundation_cards_[random_pile_index][i]) {
            card_index = i;
            break;
        }
    }

    if (card_index == -1)
        return;

    animated_foundation_cards_[random_pile_index][card_index] = true;

    double start_x = current_card_spacing_ +
                     (3 + random_pile_index) * (current_card_width_ + current_card_spacing_);
    double start_y = current_card_spacing_;

    int trajectory_choice = rand() % 100;
    int direction = rand() % 2;
    double speed = (15 + (rand() % 5));
    if (direction == 1) {
        speed *= -1;
    }

    double angle;
    if (trajectory_choice < 5) {
        angle = M_PI / 2 + (rand() % 200 - 100) / 1000.0 * M_PI / 8;
    } else if (trajectory_choice < 15) {
        if (rand() % 2 == 0) {
            angle = M_PI * 0.6 + (rand() % 500) / 1000.0 * M_PI / 6;
        } else {
            angle = M_PI * 0.4 - (rand() % 500) / 1000.0 * M_PI / 6;
        }
    } else if (trajectory_choice < 55) {
        angle = M_PI * 3 / 4 + (rand() % 1000) / 1000.0 * M_PI / 4;
    } else {
        angle = M_PI * 1 / 4 + (rand() % 1000) / 1000.0 * M_PI / 4;
    }

    AnimatedCard anim_card;
    anim_card.card = foundation_[random_pile_index][card_index];
    anim_card.x = start_x;
    anim_card.y = start_y;
    anim_card.velocity_x = cos(angle) * speed;
    anim_card.velocity_y = sin(angle) * speed;
    anim_card.rotation = 0;
    anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;
    anim_card.active = true;
    anim_card.exploded = false;
    anim_card.face_up = true;

    animated_cards_.push_back(anim_card);
    cards_launched_++;
}

void SolitaireGame::updateCardFragments_gl(AnimatedCard &card) {
    if (!card.exploded)
        return;

    int window_height = 768;

    for (auto &fragment : card.fragments) {
        if (!fragment.active)
            continue;

        fragment.x += fragment.velocity_x;
        fragment.y += fragment.velocity_y;
        fragment.velocity_y += GRAVITY;

        fragment.rotation += fragment.rotation_velocity;

        const double min_height = window_height * 0.5;
        if (fragment.y > min_height && fragment.y < window_height - fragment.height &&
            fragment.velocity_y > 0 && (rand() % 1000 < 5)) {
            fragment.velocity_y = -fragment.velocity_y * 0.8;
            fragment.velocity_x += (rand() % 11 - 5);
            fragment.rotation_velocity *= 1.5;
            playSound(GameSoundEvent::Firework);
        }

        if (fragment.x < -fragment.width || fragment.x > 1024 ||
            fragment.y > window_height + fragment.height) {
            fragment.active = false;
        }
    }
}

void SolitaireGame::explodeCard_gl(AnimatedCard &card) {
    card.exploded = true;
    playSound(GameSoundEvent::Firework);

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

            card.fragments.push_back(fragment);
        }
    }
}

// ============================================================================
// Deal Animation Functions - OpenGL Version
// ============================================================================

void SolitaireGame::startDealAnimation_gl() {
    if (deal_animation_active_)
        return;

    deal_animation_active_ = true;
    cards_dealt_ = 0;
    deal_timer_ = 0;
    deal_cards_.clear();

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onDealAnimationTick_gl, this);
}

void SolitaireGame::updateDealAnimation_gl() {
    if (!deal_animation_active_)
        return;

    deal_timer_ += ANIMATION_INTERVAL;

    if (deal_timer_ >= DEAL_INTERVAL) {
        deal_timer_ = 0;
        dealNextCard_gl();
    }

    for (auto &card : deal_cards_) {
        if (!card.active)
            continue;

        double dx = card.target_x - card.x;
        double dy = card.target_y - card.y;
        double distance = sqrt(dx * dx + dy * dy);

        if (distance < 2.0) {
            card.active = false;
        } else {
            double speed = DEAL_SPEED;
            card.x += dx * speed;
            card.y += dy * speed;
            card.rotation += 0.1;
        }
    }

    refreshDisplay();
}

gboolean SolitaireGame::onDealAnimationTick_gl(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->updateDealAnimation_gl();
    
    // Force redraw after animation update
    if (game->gl_area_) {
        gtk_widget_queue_draw(game->gl_area_);
    }
    
    return game->deal_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::dealNextCard_gl() {
    if (cards_dealt_ >= 28)
        return;

    int tableau_pile = cards_dealt_ % 7;
    int tableau_index = cards_dealt_ / 7;

    AnimatedCard anim_card;
    anim_card.card = tableau_[tableau_pile][tableau_index].card;
    anim_card.face_up = tableau_[tableau_pile][tableau_index].face_up;
    anim_card.x = current_card_spacing_;
    anim_card.y = current_card_spacing_;
    anim_card.target_x = current_card_spacing_ +
                         tableau_pile * (current_card_width_ + current_card_spacing_);
    anim_card.target_y = (current_card_spacing_ + current_card_height_ + current_vert_spacing_) +
                         tableau_index * current_vert_spacing_;
    anim_card.rotation = 0;
    anim_card.rotation_velocity = 0;
    anim_card.active = true;
    anim_card.exploded = false;

    deal_cards_.push_back(anim_card);
    cards_dealt_++;

    playSound(GameSoundEvent::DealCard);
}

void SolitaireGame::completeDeal_gl() {
    deal_animation_active_ = false;

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    deal_cards_.clear();
    refreshDisplay();
}

void SolitaireGame::stopDealAnimation_gl() {
    completeDeal_gl();
}

// ============================================================================
// Foundation Move Animation - OpenGL Version
// ============================================================================

void SolitaireGame::startFoundationMoveAnimation_gl(const cardlib::Card &card,
                                                     int source_pile,
                                                     int source_index,
                                                     int target_pile) {
    if (foundation_move_animation_active_)
        return;

    foundation_move_animation_active_ = true;
    foundation_target_pile_ = target_pile;
    foundation_move_timer_ = 0;

    double start_x = 0, start_y = 0;

    if (source_pile < 2) {
        start_x = current_card_spacing_ +
                  source_pile * (current_card_width_ + current_card_spacing_);
        start_y = current_card_spacing_;
    } else if (source_pile >= 6 && source_pile <= 12) {
        int tableau_index = source_pile - 6;
        start_x = current_card_spacing_ +
                  tableau_index * (current_card_width_ + current_card_spacing_);
        start_y = (current_card_spacing_ + current_card_height_ + current_vert_spacing_) +
                  source_index * current_vert_spacing_;
    } else {
        foundation_move_animation_active_ = false;
        return;
    }

    double target_x = current_card_spacing_ +
                      (3 + (target_pile - 2)) * (current_card_width_ + current_card_spacing_);
    double target_y = current_card_spacing_;

    foundation_move_card_.card = card;
    foundation_move_card_.x = start_x;
    foundation_move_card_.y = start_y;
    foundation_move_card_.target_x = target_x;
    foundation_move_card_.target_y = target_y;
    foundation_move_card_.velocity_x = 0;
    foundation_move_card_.velocity_y = 0;
    foundation_move_card_.rotation = 0;
    foundation_move_card_.rotation_velocity = 0;
    foundation_move_card_.active = true;
    foundation_move_card_.exploded = false;
    foundation_move_card_.face_up = true;

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onFoundationMoveAnimationTick_gl, this);

    refreshDisplay();
}

gboolean SolitaireGame::onFoundationMoveAnimationTick_gl(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->updateFoundationMoveAnimation_gl();
    
    // Force redraw after animation update
    if (game->gl_area_) {
        gtk_widget_queue_draw(game->gl_area_);
    }
    
    return game->foundation_move_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::updateFoundationMoveAnimation_gl() {
    if (!foundation_move_animation_active_)
        return;

    double dx = foundation_move_card_.target_x - foundation_move_card_.x;
    double dy = foundation_move_card_.target_y - foundation_move_card_.y;
    double distance = sqrt(dx * dx + dy * dy);

    if (distance < 5.0) {
        foundation_[foundation_target_pile_ - 2].push_back(foundation_move_card_.card);

        foundation_move_animation_active_ = false;

        if (animation_timer_id_ > 0) {
            g_source_remove(animation_timer_id_);
            animation_timer_id_ = 0;
        }

        if (!auto_finish_active_ && checkWinCondition()) {
            startWinAnimation_gl();
        }

        if (auto_finish_active_) {
            if (auto_finish_timer_id_ > 0) {
                g_source_remove(auto_finish_timer_id_);
            }
            auto_finish_timer_id_ = g_timeout_add(50, onAutoFinishTick_gl, this);
        }
    } else {
        double speed = distance * FOUNDATION_MOVE_SPEED;
        double move_x = dx * speed / distance;
        double move_y = dy * speed / distance;

        double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
        double arc_height = 30.0;
        double arc_offset = sin(progress * M_PI) * arc_height;

        foundation_move_card_.x += move_x;
        foundation_move_card_.y += move_y - arc_offset * 0.1;

        foundation_move_card_.rotation = sin(progress * M_PI * 2) * 0.1;
    }

    refreshDisplay();
}

// ============================================================================
// Stock to Waste Animation - OpenGL Version
// ============================================================================

void SolitaireGame::startStockToWasteAnimation_gl() {
    if (stock_to_waste_animation_active_ || stock_.empty())
        return;

    playSound(GameSoundEvent::CardFlip);

    stock_to_waste_animation_active_ = true;
    stock_to_waste_timer_ = 0;
    pending_waste_cards_.clear();

    int cards_to_deal = draw_three_mode_ ? std::min(3, static_cast<int>(stock_.size())) : 1;

    for (int i = 0; i < cards_to_deal; i++) {
        if (!stock_.empty()) {
            pending_waste_cards_.push_back(stock_.back());
            stock_.pop_back();
        }
    }

    if (pending_waste_cards_.empty()) {
        stock_to_waste_animation_active_ = false;
        return;
    }

    stock_to_waste_card_.card = pending_waste_cards_.back();
    stock_to_waste_card_.face_up = true;

    stock_to_waste_card_.x = current_card_spacing_;
    stock_to_waste_card_.y = current_card_spacing_;

    stock_to_waste_card_.target_x = 2 * current_card_spacing_ + current_card_width_;
    stock_to_waste_card_.target_y = current_card_spacing_;

    stock_to_waste_card_.rotation = 0;
    stock_to_waste_card_.rotation_velocity = 0;
    stock_to_waste_card_.active = true;
    stock_to_waste_card_.exploded = false;

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onStockToWasteAnimationTick_gl, this);

    refreshDisplay();
}

gboolean SolitaireGame::onStockToWasteAnimationTick_gl(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->updateStockToWasteAnimation_gl();
    
    // Force redraw after animation update
    if (game->gl_area_) {
        gtk_widget_queue_draw(game->gl_area_);
    }
    
    return game->stock_to_waste_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::updateStockToWasteAnimation_gl() {
    if (!stock_to_waste_animation_active_)
        return;

    double dx = stock_to_waste_card_.target_x - stock_to_waste_card_.x;
    double dy = stock_to_waste_card_.target_y - stock_to_waste_card_.y;
    double distance = sqrt(dx * dx + dy * dy);

    if (distance < 5.0) {
        waste_.push_back(stock_to_waste_card_.card);
        pending_waste_cards_.pop_back();
        playSound(GameSoundEvent::CardPlace);

        if (!pending_waste_cards_.empty()) {
            stock_to_waste_card_.card = pending_waste_cards_.back();
            stock_to_waste_card_.x = current_card_spacing_;
            stock_to_waste_card_.y = current_card_spacing_;
            stock_to_waste_card_.rotation = 0;
        } else {
            completeStockToWasteAnimation_gl();
            return;
        }
    } else {
        double speed = 0.3;
        double move_x = dx * speed;
        double move_y = dy * speed;

        double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
        double arc_height = 20.0;
        double arc_offset = sin(progress * M_PI) * arc_height;

        stock_to_waste_card_.x += move_x;
        stock_to_waste_card_.y += move_y - arc_offset * 0.1;

        stock_to_waste_card_.rotation = sin(progress * M_PI * 2) * 0.15;
    }

    refreshDisplay();
}

void SolitaireGame::completeStockToWasteAnimation_gl() {
    stock_to_waste_animation_active_ = false;

    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }

    pending_waste_cards_.clear();
    refreshDisplay();
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
                                        GLuint shaderProgram,
                                        GLuint VAO) {
    if (!fragment.active)
        return;

    // Note: CardFragment uses cairo_surface_t which isn't compatible with OpenGL
    // For now, we'll just render the card back as a placeholder for fragments
    
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
    
    // Bind card back texture as placeholder
    GLint texLoc = glGetUniformLocation(shaderProgram, "cardTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cardBackTexture_gl_);
    
    // Set alpha
    GLint alphaLoc = glGetUniformLocation(shaderProgram, "alpha");
    glUniform1f(alphaLoc, 0.8f);  // Slightly transparent
    
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
                    drawCardFragment_gl(fragment, shaderProgram, VAO);
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
    
    if (!validateOpenGLContext()) {
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
    
    if (!validateOpenGLContext()) {
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

bool SolitaireGame::initializeCardTextures_gl() {
    std::cout << "\nInitializing card textures..." << std::endl;
    
    if (!validateOpenGLContext()) {
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
    if (!validateOpenGLContext()) {
        std::cerr << "✗ FATAL: OpenGL context validation failed" << std::endl;
        std::cerr << "Falling back to Cairo mode" << std::endl;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ Context validated" << std::endl;
    
    std::cout << "\n[STEP 2/5] Initializing GLEW..." << std::endl;
    if (!initializeGLEW()) {
        std::cerr << "✗ FATAL: GLEW initialization failed" << std::endl;
        std::cerr << "Falling back to Cairo mode" << std::endl;
        rendering_engine_ = RenderingEngine::CAIRO;
        cairo_initialized_ = true;
        return false;
    }
    std::cout << "✓ GLEW initialized" << std::endl;
    
    std::cout << "\n[STEP 3/5] Checking GPU capabilities..." << std::endl;
    if (!checkOpenGLCapabilities()) {
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

void SolitaireGame::drawStockPile_gl() {
    int x = current_card_spacing_;
    int y = current_card_spacing_;
    
    if (stock_.empty()) {
        // Draw empty stock pile placeholder
        drawEmptyPile_gl(x, y);
    } else {
        // Draw only the top card of stock
        drawCard_gl(stock_.back(), x, y, false);
    }
}

void SolitaireGame::drawWastePile_gl() {
    int x = current_card_spacing_ + (current_card_width_ + current_card_spacing_);
    int y = current_card_spacing_;
    
    if (waste_.empty()) {
        // Draw empty waste pile placeholder (like Cairo's light green placeholder)
        drawEmptyPile_gl(x, y);
        return;
    }
    
    // Draw top card of waste pile (face up)
    drawCard_gl(waste_.back(), x, y, true);
}

void SolitaireGame::drawFoundationPiles_gl() {
    int start_x = current_card_spacing_ + (current_card_width_ + current_card_spacing_) * 3;
    int y = current_card_spacing_;
    
    for (size_t i = 0; i < foundation_.size(); i++) {
        int x = start_x + (int)(i * (current_card_width_ + current_card_spacing_));
        
        if (foundation_[i].empty()) {
            // Draw empty foundation placeholder
            drawEmptyPile_gl(x, y);
        } else {
            // Draw top card of foundation (face up)
            drawCard_gl(foundation_[i].back(), x, y, true);
        }
    }
}

void SolitaireGame::drawTableauPiles_gl() {
    int base_y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
    
    for (size_t pile = 0; pile < tableau_.size(); pile++) {
        int x = current_card_spacing_ + (int)(pile * (current_card_width_ + current_card_spacing_));
        int y = base_y;
        
        for (size_t card_idx = 0; card_idx < tableau_[pile].size(); card_idx++) {
            const TableauCard &tc = tableau_[pile][card_idx];
            int card_y = y + (int)(card_idx * current_vert_spacing_);
            
            drawCard_gl(tc.card, x, card_y, tc.face_up);
        }
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

void SolitaireGame::renderFrame_gl() {
    if (!game_fully_initialized_) {
        glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }
    
    if (rendering_engine_ != RenderingEngine::OPENGL || !opengl_initialized_) {
        return;
    }
    
    // CRITICAL FIX: Get actual window dimensions instead of hardcoding
    GtkAllocation allocation;
    gtk_widget_get_allocation(game_area_, &allocation);
    
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
    drawWastePile_gl();
    drawFoundationPiles_gl();
    drawTableauPiles_gl();
    
    // Disable blending after drawing
    glDisable(GL_BLEND);
    
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
