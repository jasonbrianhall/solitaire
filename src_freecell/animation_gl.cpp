#include "freecell.h"
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

#define GRAVITY 0.3
#define EXPLOSION_THRESHOLD_MIN 0.35
#define EXPLOSION_THRESHOLD_MAX 0.7

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

bool FreecellGame::validateOpenGLContext() {
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

bool FreecellGame::initializeGLEW() {
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

bool FreecellGame::checkOpenGLCapabilities() {
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

void FreecellGame::logOpenGLInfo() {
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
// WIN ANIMATION - OpenGL 3.4 Version
// ============================================================================
// NOTE: onAnimationTick is defined in animation.cpp and calls updateWinAnimation_gl()
//       when rendering engine is OpenGL

void FreecellGame::updateWinAnimation_gl() {
  if (!win_animation_active_)
    return;

  // Launch new cards periodically
  launch_timer_ += ANIMATION_INTERVAL;
  if (launch_timer_ >= 100) { // Launch a new card every 100ms
    launch_timer_ = 0;
    if (rand() % 100 < 10) {
        // Launch multiple cards in rapid succession
        for (int i = 0; i < 4; i++) {
            // Alternate between foundation and freecell launches
            if (i % 2 == 0) {
                launchNextCard_gl();        // Launch from foundation
            } else {
                launchCardFromFreecell(); // Launch from freecell area
            }
        }
    } else {    
       // Randomly choose launch source
       if (rand() % 2 == 0) {
           launchNextCard_gl();          // Launch from foundation
       } else {
           launchCardFromFreecell();  // Launch from freecell area
       }
    }
  }

  // Update physics for all active cards
  bool all_cards_finished = true;
  GtkAllocation allocation;
  gtk_widget_get_allocation(gl_area_, &allocation);

  const double explosion_min = allocation.height * EXPLOSION_THRESHOLD_MIN;
  const double explosion_max = allocation.height * EXPLOSION_THRESHOLD_MAX;

  for (auto &card : animated_cards_) {
    if (!card.active)
      continue;

    if (!card.exploded) {
      // Update position
      card.x += card.velocity_x;
      card.y += card.velocity_y;
      card.velocity_y += GRAVITY;

      // Update rotation
      card.rotation += card.rotation_velocity;

      // Check if card should explode (increase random chance from 2% to 5%)
      if (card.y > explosion_min && card.y < explosion_max &&
          (rand() % 100 < 5)) {
        explodeCard_gl(card);
      }

      // Check if card is off screen
      if (card.x < -current_card_width_ || card.x > allocation.width ||
          card.y > allocation.height + current_card_height_) {
        card.active = false;
      } else {
        all_cards_finished = false;
      }
    } else {
      // Update explosion fragments
      updateCardFragments_gl(card);

      // Check if all fragments are inactive
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

  // Clear inactive cards periodically to prevent memory bloat
  if (animated_cards_.size() > 200) {
    // Manual removal of inactive cards without using std::remove_if
    std::vector<AnimatedCard> active_cards;
    for (const auto& card : animated_cards_) {
      if (card.active) {
        active_cards.push_back(card);
      }
    }
    animated_cards_ = active_cards;
  }

  refreshDisplay();
}

void FreecellGame::startWinAnimation_gl() {
  win_animation_active_ = true;
  cards_launched_ = 0;
  launch_timer_ = 0;
  animated_cards_.clear();
  
  if (animation_timer_id_ == 0) {
    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
  }
}

void FreecellGame::stopWinAnimation_gl() {
  win_animation_active_ = false;
  if (animation_timer_id_ != 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }
  animated_cards_.clear();
  freecell_animation_cards_.clear();
}

void FreecellGame::launchNextCard_gl() {
  // Try each foundation pile in sequence, cycling through them
  static int current_pile_index = 0;
  
  GtkAllocation allocation;
  gtk_widget_get_allocation(gl_area_, &allocation);
  
  // Try all piles if needed
  for (int attempts = 0; attempts < foundation_.size(); attempts++) {
    int pile_index = (current_pile_index + attempts) % foundation_.size();
    
    // Check if this pile has any cards
    if (!foundation_[pile_index].empty()) {
      // Calculate the starting X position based on the pile
      double start_x = allocation.width - (4 - pile_index) * (current_card_width_ + current_card_spacing_);
      double start_y = current_card_spacing_;

      // Randomize launch trajectory
      int trajectory_choice = rand() % 100;
      int direction = rand() % 2;
      double speed = (15 + (rand() % 5)) * (direction ? 1 : -1);

      double angle;
      if (trajectory_choice < 5) {
        // 5% chance to go straight up
        angle = G_PI / 2 + (rand() % 200 - 100) / 1000.0 * G_PI / 8;
      } else if (trajectory_choice < 15) {
        // 10% chance for high arc launch
        angle = (rand() % 2 == 0) ? 
          (G_PI * 0.6 + (rand() % 500) / 1000.0 * G_PI / 6) : 
          (G_PI * 0.4 - (rand() % 500) / 1000.0 * G_PI / 6);
      } else {
        // Otherwise, spread left and right
        angle = trajectory_choice < 85 ? 
          (G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4) : 
          (G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4);
      }

      // Create animated card
      AnimatedCard anim_card;
      anim_card.card = foundation_[pile_index].back();
      anim_card.x = start_x;
      anim_card.y = start_y;
      anim_card.velocity_x = cos(angle) * speed;
      anim_card.velocity_y = sin(angle) * speed;
      anim_card.rotation = 0;
      anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;
      anim_card.active = true;
      anim_card.exploded = false;
      anim_card.face_up = true;
      anim_card.source_pile = pile_index;

      // Remove the card from the foundation pile
      cardlib::Card card = foundation_[pile_index].back();
      foundation_[pile_index].pop_back();
      
      // Add the card to the BOTTOM of the same foundation pile
      foundation_[pile_index].insert(foundation_[pile_index].begin(), card);

      // Add to animated cards
      animated_cards_.push_back(anim_card);
      cards_launched_++;
      
      // Move to the next pile for the next card
      current_pile_index = (pile_index + 1) % foundation_.size();
      return;
    }
  }
  
  // If we get here, there were no cards in any foundation pile
  current_pile_index = (current_pile_index + 1) % foundation_.size();
}

void FreecellGame::explodeCard_gl(AnimatedCard &card) {
  // For OpenGL, we reuse the same explosion fragment logic
  // Mark the card as exploded
  card.exploded = true;

  playSound(GameSoundEvent::Firework);

  // Create fragments
  card.fragments.clear();

  // Split the card into smaller fragments for more dramatic effect (4x4 grid)
  const int grid_size = 4;
  const int fragment_width = current_card_width_ / grid_size;
  const int fragment_height = current_card_height_ / grid_size;

  for (int row = 0; row < grid_size; row++) {
    for (int col = 0; col < grid_size; col++) {
      CardFragment fragment;

      // Initial position
      fragment.x = card.x + col * fragment_width;
      fragment.y = card.y + row * fragment_height;
      fragment.width = fragment_width;
      fragment.height = fragment_height;

      // Calculate distance from center of the card
      double center_x = card.x + current_card_width_ / 2;
      double center_y = card.y + current_card_height_ / 2;
      double fragment_center_x = fragment.x + fragment_width / 2;
      double fragment_center_y = fragment.y + fragment_height / 2;

      // Direction vector from center of card
      double dir_x = fragment_center_x - center_x;
      double dir_y = fragment_center_y - center_y;

      // Normalize direction vector
      double magnitude = sqrt(dir_x * dir_x + dir_y * dir_y);
      if (magnitude > 0.001) {
        dir_x /= magnitude;
        dir_y /= magnitude;
      } else {
        // If fragment is at center, give it a random direction
        double rand_angle = 2.0 * G_PI * (rand() % 1000) / 1000.0;
        dir_x = cos(rand_angle);
        dir_y = sin(rand_angle);
      }

      // Set velocity based on direction and random speed
      double speed = 3.0 + (rand() % 50) / 10.0;
      fragment.velocity_x = dir_x * speed;
      fragment.velocity_y = dir_y * speed;

      // Rotation
      fragment.rotation = (rand() % 360);
      fragment.rotation_velocity = (rand() % 20 - 10);

      // Mark as active
      fragment.active = true;
      fragment.surface = nullptr;

      card.fragments.push_back(fragment);
    }
  }
}

void FreecellGame::updateCardFragments_gl(AnimatedCard &card) {
  for (auto &fragment : card.fragments) {
    if (!fragment.active)
      continue;

    // Update position
    fragment.x += fragment.velocity_x;
    fragment.y += fragment.velocity_y;
    fragment.velocity_y += GRAVITY;

    // Update rotation
    fragment.rotation += fragment.rotation_velocity;

    // Check if fragment is off screen
    GtkAllocation allocation;
    gtk_widget_get_allocation(gl_area_, &allocation);
    
    if (fragment.x < -50 || fragment.x > allocation.width + 50 ||
        fragment.y > allocation.height + 50) {
      fragment.active = false;
    }
  }
}

// ============================================================================
// DEAL ANIMATION - OpenGL 3.4 Version
// ============================================================================

gboolean FreecellGame::onDealAnimationTick_gl(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->updateDealAnimation_gl();
  return game->deal_animation_active_ ? TRUE : FALSE;
}

void FreecellGame::startDealAnimation_gl() {
  deal_animation_active_ = true;
  cards_dealt_ = 0;
  deal_timer_ = 0;
  deal_cards_.clear();
  
  // Schedule the first card deal
  if (animation_timer_id_ == 0) {
    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onDealAnimationTick_gl, this);
  }
}

void FreecellGame::updateDealAnimation_gl() {
  if (!deal_animation_active_)
    return;

  deal_timer_ += ANIMATION_INTERVAL;

  // Deal a new card periodically
  if (deal_timer_ >= DEAL_INTERVAL) {
    deal_timer_ = 0;
    dealNextCard_gl();
  }

  // Update all dealing cards
  bool all_cards_settled = true;
  GtkAllocation allocation;
  gtk_widget_get_allocation(gl_area_, &allocation);

  for (auto &card : deal_cards_) {
    if (!card.active)
      continue;

    // Lerp towards target position
    double dx = card.target_x - card.x;
    double dy = card.target_y - card.y;
    double distance = sqrt(dx * dx + dy * dy);

    if (distance < 5.0) {
      card.x = card.target_x;
      card.y = card.target_y;
    } else {
      // Move towards target with DEAL_SPEED multiplier
      card.x += dx * DEAL_SPEED * (DEAL_INTERVAL / 16.0);
      card.y += dy * DEAL_SPEED * (DEAL_INTERVAL / 16.0);
      all_cards_settled = false;
    }
  }

  // Check if we've dealt all cards and they're all settled
  if (cards_dealt_ >= 52 && all_cards_settled) {
    completeDeal_gl();
  }

  refreshDisplay();
}

void FreecellGame::dealNextCard_gl() {
  if (cards_dealt_ >= 52)
    return;

  // Determine which column this card goes to
  int column_index = cards_dealt_ % 8;
  int card_index = cards_dealt_ / 8;

  // Create animated card
  AnimatedCard anim_card;
  anim_card.card = tableau_[column_index][card_index];
  
  // Start position (off-screen or at stock)
  anim_card.x = 0; // or stock position
  anim_card.y = 0;
  
  // Target position in tableau
  int x = current_card_spacing_ + column_index * (current_card_width_ + current_card_spacing_);
  int y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
          card_index * current_vert_spacing_;
  
  anim_card.target_x = x;
  anim_card.target_y = y;
  anim_card.active = true;
  anim_card.face_up = true;
  anim_card.source_pile = 8 + column_index; // Tableau piles

  deal_cards_.push_back(anim_card);
  cards_dealt_++;
}

void FreecellGame::completeDeal_gl() {
  deal_animation_active_ = false;
  if (animation_timer_id_ != 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }
  deal_cards_.clear();
}

void FreecellGame::stopDealAnimation_gl() {
  deal_animation_active_ = false;
  if (animation_timer_id_ != 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }
  deal_cards_.clear();
}

// ============================================================================
// FOUNDATION MOVE ANIMATION - OpenGL 3.4 Version
// ============================================================================

gboolean FreecellGame::onFoundationMoveAnimationTick_gl(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->updateFoundationMoveAnimation_gl();
  return game->foundation_move_animation_active_ ? TRUE : FALSE;
}

void FreecellGame::startFoundationMoveAnimation_gl(const cardlib::Card &card,
                                                    int source_pile,
                                                    int source_index,
                                                    int target_pile) {
  foundation_move_animation_active_ = true;
  foundation_move_card_.card = card;
  foundation_move_card_.active = true;
  foundation_move_card_.exploded = false;
  foundation_move_card_.face_up = true;
  
  foundation_source_pile_ = source_pile;
  foundation_target_pile_ = target_pile;
  foundation_move_timer_ = 0;

  // Calculate starting position based on source pile
  int num_freecells = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  int foundation_start = num_freecells;
  
  if (source_pile < foundation_start) {
    // From freecell
    foundation_move_card_.x = current_card_spacing_ + source_pile * (current_card_width_ + current_card_spacing_);
    foundation_move_card_.y = current_card_spacing_;
  } else if (source_pile < foundation_start + 4) {
    // From foundation
    int foundation_idx = source_pile - foundation_start;
    foundation_move_card_.x = allocation.width - (4 - foundation_idx) * (current_card_width_ + current_card_spacing_);
    foundation_move_card_.y = current_card_spacing_;
  } else {
    // From tableau
    int tableau_idx = source_pile - (foundation_start + 4);
    foundation_move_card_.x = current_card_spacing_ + tableau_idx * (current_card_width_ + current_card_spacing_);
    foundation_move_card_.y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
                              source_index * current_vert_spacing_;
  }

  // Calculate target position in foundation
  int target_foundation_idx = target_pile - (foundation_start + 4);
  foundation_move_card_.target_x = allocation.width - (4 - target_foundation_idx) * (current_card_width_ + current_card_spacing_);
  foundation_move_card_.target_y = current_card_spacing_;

  if (animation_timer_id_ == 0) {
    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onFoundationMoveAnimationTick_gl, this);
  }
}

void FreecellGame::updateFoundationMoveAnimation_gl() {
  if (!foundation_move_animation_active_)
    return;

  foundation_move_timer_ += ANIMATION_INTERVAL / 1000.0; // Convert to seconds

  // Linear interpolation from source to target
  double progress = foundation_move_timer_ / FOUNDATION_MOVE_SPEED;
  
  if (progress >= 1.0) {
    progress = 1.0;
    foundation_move_animation_active_ = false;
    if (animation_timer_id_ != 0) {
      g_source_remove(animation_timer_id_);
      animation_timer_id_ = 0;
    }
  }

  foundation_move_card_.x = foundation_move_card_.x + 
                            (foundation_move_card_.target_x - foundation_move_card_.x) * progress;
  foundation_move_card_.y = foundation_move_card_.y + 
                            (foundation_move_card_.target_y - foundation_move_card_.y) * progress;

  refreshDisplay();
}

// ============================================================================
// AUTO-FINISH ANIMATION - OpenGL 3.4 Version
// ============================================================================
// NOTE: onAutoFinishTick is defined in animation.cpp and calls processNextAutoFinishMove_gl()
//       when rendering engine is OpenGL

void FreecellGame::processNextAutoFinishMove_gl() {
    if (!auto_finish_active_)
        return;

    // Delegate to the main Cairo-based auto-finish logic
    // The animation rendering will be handled by OpenGL
    if (!autoFinishMoves()) {
        auto_finish_active_ = false;
        if (auto_finish_timer_id_ != 0) {
            g_source_remove(auto_finish_timer_id_);
            auto_finish_timer_id_ = 0;
        }
    }
    
    refreshDisplay();
}

// ============================================================================
// DRAWING FUNCTIONS - OpenGL 3.4 Version
// ============================================================================

void FreecellGame::drawAnimatedCard_gl(const AnimatedCard &anim_card, GLuint shaderProgram, GLuint VAO) {
  if (!anim_card.active) {
    return;
  }

  glUseProgram(shaderProgram);

  // Setup matrices
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(anim_card.x, anim_card.y, 0.0f));
  model = glm::rotate(model, static_cast<float>(anim_card.rotation), glm::vec3(0.0f, 0.0f, 1.0f));

  GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
  glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

  // Draw the card
  drawCard_gl(anim_card.card, static_cast<int>(anim_card.x), static_cast<int>(anim_card.y), anim_card.face_up);
}

void FreecellGame::drawCardFragment_gl(const CardFragment &fragment, const AnimatedCard &card, GLuint shaderProgram, GLuint VAO) {
  if (!fragment.active) {
    return;
  }

  glUseProgram(shaderProgram);

  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(fragment.x, fragment.y, 0.0f));
  model = glm::rotate(model, static_cast<float>(fragment.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::scale(model, glm::vec3(fragment.width, fragment.height, 1.0f));

  GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
  glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

  // Draw fragment (reuse card drawing with subset)
  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void FreecellGame::drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO) {
  for (const auto &anim_card : animated_cards_) {
    if (!anim_card.active) {
      continue;
    }

    if (!anim_card.exploded) {
      // Draw the whole card with rotation
      drawAnimatedCard_gl(anim_card, shaderProgram, VAO);
    } else {
      // Draw all the fragments for this card
      for (const auto &fragment : anim_card.fragments) {
        if (fragment.active) {
          drawCardFragment_gl(fragment, anim_card, shaderProgram, VAO);
        }
      }
    }
  }
}

void FreecellGame::drawDealAnimation_gl(GLuint shaderProgram, GLuint VAO) {
  for (const auto &anim_card : deal_cards_) {
    if (anim_card.active) {
      drawAnimatedCard_gl(anim_card, shaderProgram, VAO);
    }
  }
}

void FreecellGame::drawFoundationAnimation_gl(GLuint shaderProgram, GLuint VAO) {
  if (foundation_move_animation_active_) {
    drawAnimatedCard_gl(foundation_move_card_, shaderProgram, VAO);
  }
}

void FreecellGame::drawStockToWasteAnimation_gl(GLuint shaderProgram, GLuint VAO) {
  // Placeholder for future stock-to-waste animation
}

void FreecellGame::drawDraggedCards_gl(GLuint shaderProgram, GLuint VAO) {
  if (dragging_ && drag_card_.has_value()) {
    int drag_x = static_cast<int>(drag_start_x_ - drag_offset_x_);
    int drag_y = static_cast<int>(drag_start_y_ - drag_offset_y_);
    
    // If dragging multiple cards from tableau, draw them all with proper spacing
    if (drag_source_pile_ >= 8 && drag_cards_.size() > 1) {
      for (size_t i = 0; i < drag_cards_.size(); i++) {
        int card_y = drag_y + i * current_vert_spacing_;
        drawCard_gl(drag_cards_[i], drag_x, card_y, true);
      }
    } else {
      // Just draw the single card
      drawCard_gl(drag_card_.value(), drag_x, drag_y, true);
    }
  }
}

void FreecellGame::highlightSelectedCard_gl() {
  // Placeholder for keyboard navigation highlight in OpenGL
}

// ============================================================================
// OPENGL SHADER AND RESOURCE SETUP
// ============================================================================

GLuint FreecellGame::setupShaders_gl() {
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

GLuint loadTextureFromMemory(const std::vector<unsigned char> &data) {
    if (data.empty()) {
        std::cerr << "  ✗ Error: Empty image data provided to loadTextureFromMemory" << std::endl;
        return 0;
    }
    
    int width, height, channels;
    unsigned char *img_data = stbi_load_from_memory(
        data.data(), 
        data.size(), 
        &width, &height, &channels, 
        STBI_rgb_alpha
    );
    
    if (!img_data) {
        std::cerr << "  ✗ Error: Failed to load image from memory: " << stbi_failure_reason() << std::endl;
        return 0;
    }
    
    std::cout << "  ✓ Image loaded from memory: " << width << "x" << height << " (" << channels << " channels)" << std::endl;
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    stbi_image_free(img_data);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return texture;
}

GLuint FreecellGame::setupCardQuadVAO_gl() {
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

bool FreecellGame::initializeCardTextures_gl() {
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

void FreecellGame::drawCard_gl(const cardlib::Card &card, int x, int y, bool face_up) {
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

void FreecellGame::drawEmptyPile_gl(int x, int y) {
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

// ============================================================================
// OPENGL RENDERING FRAME
// ============================================================================

void FreecellGame::renderFrame_gl() {
    if (!game_fully_initialized_) {
        glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }
    
    if (rendering_engine_ != RenderingEngine::OPENGL || !opengl_initialized_) {
        return;
    }
    
    // Get actual window dimensions
    GtkAllocation allocation;
    gtk_widget_get_allocation(gl_area_, &allocation);
    
    // Clear screen
    glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Set viewport to match actual window size
    glViewport(0, 0, allocation.width, allocation.height);
    
    // Setup matrices
    glUseProgram(cardShaderProgram_gl_);
    
    glm::mat4 projection = glm::ortho(0.0f, (float)allocation.width, 
                                      (float)allocation.height, 0.0f, -1.0f, 1.0f);
    GLint projLoc = glGetUniformLocation(cardShaderProgram_gl_, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    
    glm::mat4 view = glm::mat4(1.0f);
    GLint viewLoc = glGetUniformLocation(cardShaderProgram_gl_, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Draw all game piles (foundation, freecells, tableau, etc.)
    // This would call drawFoundationPiles_gl(), drawTableau_gl(), etc.
    
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
    
    // Draw dragged cards overlay
    drawDraggedCards_gl(cardShaderProgram_gl_, cardQuadVAO_gl_);
    
    // Draw keyboard navigation highlight if active
    if (keyboard_navigation_active_ && !dragging_ &&
        !deal_animation_active_ && !win_animation_active_ &&
        !foundation_move_animation_active_) {
        highlightSelectedCard_gl();
    }
}

// ============================================================================
// CLEANUP AND HELPER FUNCTIONS
// ============================================================================

void FreecellGame::cleanupOpenGLResources_gl() {
    if (cardShaderProgram_gl_ != 0) {
        glDeleteProgram(cardShaderProgram_gl_);
        cardShaderProgram_gl_ = 0;
    }
    
    if (simpleShaderProgram_gl_ != 0) {
        glDeleteProgram(simpleShaderProgram_gl_);
        simpleShaderProgram_gl_ = 0;
    }
    
    if (cardQuadVAO_gl_ != 0) {
        glDeleteVertexArrays(1, &cardQuadVAO_gl_);
        cardQuadVAO_gl_ = 0;
    }
    
    if (cardQuadVBO_gl_ != 0) {
        glDeleteBuffers(1, &cardQuadVBO_gl_);
        cardQuadVBO_gl_ = 0;
    }
    
    if (cardQuadEBO_gl_ != 0) {
        glDeleteBuffers(1, &cardQuadEBO_gl_);
        cardQuadEBO_gl_ = 0;
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
