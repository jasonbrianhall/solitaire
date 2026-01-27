#include "pyramid.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <shlobj.h>
#include <appmodel.h>
#include <vector>
#else
#include <unistd.h>
#include <dirent.h>
#endif

#ifdef _WIN32
std::string getExecutableDir() { 
    char buffer[MAX_PATH]; 
    GetModuleFileNameA(NULL, buffer, MAX_PATH); 
    std::string path(buffer); size_t pos = path.find_last_of("\\/"); 
    return (pos == std::string::npos) ? "." : path.substr(0, pos); 
}
#endif

std::string getDirectoryStructure(const std::string &directory = ".") {
  std::string result;
  result += "=== Directory Structure ===\n";
  result += "Directory: " + directory + "\n";
  result += "Absolute path: ";
  
  char cwd[1024];
#ifdef _WIN32
  if (_getcwd(cwd, sizeof(cwd)) != nullptr) {
#else
  if (getcwd(cwd, sizeof(cwd)) != nullptr) {
#endif
    result += cwd;
  } else {
    result += "(unable to determine)";
  }
  result += "\n\nContents:\n";
  
#ifdef _WIN32
  WIN32_FIND_DATAA findData;
  HANDLE findHandle = FindFirstFileA((directory + "\\*").c_str(), &findData);
  
  if (findHandle != INVALID_HANDLE_VALUE) {
    do {
      std::string name = findData.cFileName;
      std::string type = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "[DIR]" : "[FILE]";
      result += "  " + type + " " + name + "\n";
    } while (FindNextFileA(findHandle, &findData));
    FindClose(findHandle);
  } else {
    result += "  (unable to read directory)\n";
  }
#else
  DIR *dir = opendir(directory.c_str());
  if (dir != nullptr) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string name = entry->d_name;
      std::string type = (entry->d_type == DT_DIR) ? "[DIR]" : "[FILE]";
      result += "  " + type + " " + name + "\n";
    }
    closedir(dir);
  } else {
    result += "  (unable to read directory)\n";
  }
#endif
  
  result += "=== End Directory Structure ===\n";
  return result;
}

PyramidGame::PyramidGame()
    : dragging_(false), drag_source_(nullptr), drag_source_pile_(-1),
      window_(nullptr), game_area_(nullptr), gl_area_(nullptr),
      rendering_stack_(nullptr), buffer_surface_(nullptr),
      buffer_cr_(nullptr), draw_three_mode_(false),
      current_card_width_(BASE_CARD_WIDTH),
      current_card_height_(BASE_CARD_HEIGHT),
      current_card_spacing_(BASE_CARD_SPACING),
      current_vert_spacing_(BASE_VERT_SPACING), is_fullscreen_(false),
      selected_pile_(-1), selected_card_idx_(-1),
      keyboard_navigation_active_(false), keyboard_selection_active_(false),
      source_pile_(-1), source_card_idx_(-1),
      current_game_mode_(GameMode::STANDARD_PYRAMID),
      multi_deck_(1),
      sound_enabled_(true),
      #ifdef __linux__
      rendering_engine_(RenderingEngine::CAIRO),
      #else
      rendering_engine_(RenderingEngine::CAIRO),
      #endif
      opengl_initialized_(false),
      cairo_initialized_(false),
      engine_switch_requested_(false),
      requested_engine_(RenderingEngine::CAIRO),
#ifdef _WIN32
      sounds_zip_path_(getExecutableDir() + "\\sound.zip"),
#else
      sounds_zip_path_("sound.zip"),
#endif
      current_seed_(0) {
  srand(time(NULL));
  current_seed_ = rand();
  initializeSettingsDir();
  
  // Load engine preference and initialize rendering
  loadEnginePreference();
  initializeRenderingEngine();
  
  loadSettings();
}

// ============================================================================
// ENGINE SWITCHING IMPLEMENTATION
// ============================================================================

bool PyramidGame::isOpenGLSupported() const {
  #ifndef USEOPENGL
  return false;
  #else
  return true;
  #endif
}

bool PyramidGame::setRenderingEngine(RenderingEngine engine) {
  #ifdef USEOPENGL
  if (engine == RenderingEngine::OPENGL) {
    std::cout << "OpenGL not supported on Windows. Using Cairo." << std::endl;
    rendering_engine_ = RenderingEngine::CAIRO;
    return true;
  }
  #endif

  if (rendering_engine_ == engine) {
    return true;
  }

  if (!opengl_initialized_ && !cairo_initialized_) {
    rendering_engine_ = engine;
    std::cout << "Rendering engine: " << getRenderingEngineName() << std::endl;
    return true;
  }

  engine_switch_requested_ = true;
  requested_engine_ = engine;
  return true;
}

bool PyramidGame::initializeRenderingEngine() {
  #ifndef USEOPENGL
  if (rendering_engine_ == RenderingEngine::OPENGL) {
    rendering_engine_ = RenderingEngine::CAIRO;
  }
  #endif

  switch (rendering_engine_) {
    case RenderingEngine::CAIRO:
      cairo_initialized_ = true;
      fprintf(stderr, "✓ Using Cairo rendering (CPU-based)\n");
      return true;

    case RenderingEngine::OPENGL:
      #ifdef __linux__
      // ✅ OpenGL initialization is deferred to onGLRealize callback
      // This happens when GTK GL area is realized (after gtk_widget_show_all)
      fprintf(stderr, "✓ OpenGL will be initialized when window is ready\n");
      return true;
      #else
      rendering_engine_ = RenderingEngine::CAIRO;
      cairo_initialized_ = true;
      return true;
      #endif

    default:
      rendering_engine_ = RenderingEngine::CAIRO;
      cairo_initialized_ = true;
      return true;
  }
}

bool PyramidGame::switchRenderingEngine(RenderingEngine newEngine) {
  #ifndef USEOPENGL
  if (newEngine == RenderingEngine::OPENGL) {
    return false;
  }
  #endif

  if (newEngine == rendering_engine_) {
    return true;
  }

  #ifdef __linux__
  if (!rendering_stack_) {
    std::cout << "Rendering stack not initialized" << std::endl;
    return false;
  }
  
  // MARK CACHE AS DIRTY - Force complete cache rebuild when switching engines
  cache_dirty_ = true;
  
  // Switch rendering engine
  rendering_engine_ = newEngine;
  
  if (newEngine == RenderingEngine::OPENGL) {
    cairo_initialized_ = false;
    opengl_initialized_ = true;  // ✅ CRITICAL FIX: Must set to true for rendering to work
  } else {
    opengl_initialized_ = false;
    cairo_initialized_ = true;
  }
  
  // Use GtkStack to switch - this preserves GL context
  const char *view = (newEngine == RenderingEngine::OPENGL) ? "opengl" : "cairo";
  gtk_stack_set_visible_child_name(GTK_STACK(rendering_stack_), view);
  
  rendering_engine_ = newEngine;
  
  // Grab focus to the new widget
  GtkWidget *current_widget = gtk_stack_get_visible_child(GTK_STACK(rendering_stack_));
  if (current_widget) {
    gtk_widget_grab_focus(current_widget);
    gtk_widget_queue_draw(current_widget);
  }
  
  // CLEAR AND REBUILD ALL CACHES - Forces complete redraw of entire screen
  clearAndRebuildCaches();
  
  refreshDisplay();
  
  saveEnginePreference();
  std::cout << "Switched to " << getRenderingEngineName() << std::endl;
  return true;
  #else
  return false;
  #endif
}

// ============================================================================
// GL CONTEXT CALLBACKS - FIX FOR NO OPENGL CONTEXT ERROR
// ============================================================================

#ifdef USEOPENGL
// Called by GTK when GL context is created and available
gboolean PyramidGame::onGLRealize(GtkGLArea *area, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  
  // Make the GL context current
  gtk_gl_area_make_current(area);
  
  fprintf(stderr, "[GL] Realize callback - initializing OpenGL resources\n");
  
  // DIAGNOSTIC: Check if GL context is actually available
  const GLubyte *version = glGetString(GL_VERSION);
  if (version == nullptr) {
    fprintf(stderr, "[GL] ERROR: No GL context available after gtk_gl_area_make_current()\n");
    fprintf(stderr, "[GL] This means GtkGLArea failed to create/bind the GL context\n");
    game->rendering_engine_ = RenderingEngine::CAIRO;
    return FALSE;
  }
  fprintf(stderr, "[GL] GL Context verified: %s\n", (const char*)version);
  
  // Initialize GL resources (NOW safe to call GL functions!)
  if (!game->initializeOpenGLResources()) {
    fprintf(stderr, "[GL] Failed to initialize OpenGL resources\n");
    gtk_gl_area_set_error(area, NULL);
    // Fall back to Cairo
    game->rendering_engine_ = RenderingEngine::CAIRO;
    return FALSE;
  }
  
  fprintf(stderr, "[GL] OpenGL resources initialized successfully\n");
  return TRUE;
}
#endif

#ifdef USEOPENGL
// Called by GTK every frame to render
gboolean PyramidGame::onGLRender(GtkGLArea *area, GdkGLContext *context, gpointer data) {
  (void)context;
  PyramidGame *game = static_cast<PyramidGame *>(data);
  
  // Ensure GL context is current before calling GL functions
  gtk_gl_area_make_current(area);
  
  int window_width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
  int window_height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
  
  if (window_width < 10 || window_height < 10) {
    gtk_widget_queue_draw(GTK_WIDGET(area));
    return TRUE;
  }
  
  // Call the actual rendering function
  game->renderFrame_gl();
  
  glFlush();
  
  // Request next frame
  gtk_widget_queue_draw(GTK_WIDGET(area));
  
  return TRUE;
}
#endif

// ============================================================================
// GL INITIALIZATION - DEFERRED FROM CONSTRUCTOR
// ============================================================================

#ifdef USEOPENGL
// Called from realize callback - NOW has GL context
bool PyramidGame::initializeOpenGLResources() {
  #ifdef __linux__
  fprintf(stderr, "[GL] Setting up OpenGL rendering...\n");
  
  // Initialize GLEW (only once)
  static gboolean glew_initialized = FALSE;
  if (!glew_initialized) {
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    
    fprintf(stderr, "[GL] glewInit returned: %i\n", err);
    fprintf(stderr, "[GL] glewInit error string: %s\n", glewGetErrorString(err));
    
    if (err != GLEW_OK) {
      // DIAGNOSTIC: Try to see if basic GL functions work anyway
      const GLubyte *vendor = glGetString(GL_VENDOR);
      if (vendor != nullptr) {
        fprintf(stderr, "[GL] WARNING: glewInit failed but GL_VENDOR is accessible: %s\n", (const char*)vendor);
        fprintf(stderr, "[GL] GL is functional despite glewInit error - proceeding anyway\n");
        glew_initialized = TRUE;
        is_glew_initialized_ = true;  // ← ALSO SET THE MEMBER VARIABLE
      } else {
        fprintf(stderr, "[GL] FATAL: glewInit failed and GL functions unavailable\n");
        fprintf(stderr, "[GL] glewInit failed: %s %i\n", glewGetErrorString(err), err);
        return false;
      }
    } else {
      glew_initialized = TRUE;
      is_glew_initialized_ = true;  // ← SET THE MEMBER VARIABLE
      fprintf(stderr, "[GL] GLEW initialized successfully\n");
    }
  } else {
    fprintf(stderr, "[GL] GLEW already initialized\n");
  }
  
  // ✅ NOW safe to call GL functions - context exists!
  
  // Setup shaders
  cardShaderProgram_gl_ = setupShaders_gl();
  if (cardShaderProgram_gl_ == 0) {
    fprintf(stderr, "[GL] Failed to setup shaders\n");
    return false;
  }
  fprintf(stderr, "[GL] Shaders compiled successfully\n");
  
  // Setup vertex array and buffers
  cardQuadVAO_gl_ = setupCardQuadVAO_gl();
  if (cardQuadVAO_gl_ == 0) {
    fprintf(stderr, "[GL] Failed to setup VAO\n");
    return false;
  }
  fprintf(stderr, "[GL] VAO setup complete\n");
  
  // Load card textures
  if (!initializeCardTextures_gl()) {
    fprintf(stderr, "[GL] Failed to initialize card textures\n");
    return false;
  }
  fprintf(stderr, "[GL] Card textures loaded\n");
  
  // Set GL state
  glClearColor(0.0f, 0.6f, 0.0f, 1.0f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_MULTISAMPLE);
  
  opengl_initialized_ = true;
  cairo_initialized_ = false;  // Disable Cairo when using OpenGL
  
  fprintf(stderr, "[GL] OpenGL initialization complete\n");
  return true;
  #else
  return false;
  #endif
}
#endif

// ============================================================================
// WIDGET SETUP - SEPARATED INTO CAIRO AND GL
// ============================================================================

void PyramidGame::setupCairoArea() {
  // Create new drawing area for Cairo rendering
  game_area_ = gtk_drawing_area_new();
  
  // Enable mouse event handling
  gtk_widget_add_events(
      game_area_,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
          GDK_POINTER_MOTION_MASK |
          GDK_STRUCTURE_MASK);

  // Connect all necessary signals
  g_signal_connect(G_OBJECT(game_area_), "draw", G_CALLBACK(onDraw), this);

  g_signal_connect(G_OBJECT(game_area_), "button-press-event",
                   G_CALLBACK(onButtonPress), this);

  g_signal_connect(G_OBJECT(game_area_), "button-release-event",
                   G_CALLBACK(onButtonRelease), this);

  g_signal_connect(G_OBJECT(game_area_), "motion-notify-event",
                   G_CALLBACK(onMotionNotify), this);

  // Add size-allocate signal handler for resize events
  g_signal_connect(
      G_OBJECT(game_area_), "size-allocate",
      G_CALLBACK(
          +[](GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
            PyramidGame *game = static_cast<PyramidGame *>(data);
            game->updateCardDimensions(allocation->width, allocation->height);

            // Recreate buffer surface with new dimensions if needed
            if (game->buffer_surface_) {
              cairo_surface_destroy(game->buffer_surface_);
              cairo_destroy(game->buffer_cr_);
            }

            game->buffer_surface_ = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, allocation->width, allocation->height);
            game->buffer_cr_ = cairo_create(game->buffer_surface_);

            gtk_widget_queue_draw(widget);
          }),
      this);

  // Set minimum size to prevent cards from becoming too small
  gtk_widget_set_size_request(
      game_area_,
      BASE_CARD_WIDTH * 7 +
          BASE_CARD_SPACING * 8, // Minimum width for 7 cards + spacing
      BASE_CARD_HEIGHT * 2 +
          BASE_VERT_SPACING * 6 // Minimum height for 2 rows + tableau
  );

  // Initialize card dimensions based on initial window size
  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);
  updateCardDimensions(allocation.width, allocation.height);
  
  // CREATE CAIRO BUFFER - CRITICAL!
  buffer_surface_ = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
  buffer_cr_ = cairo_create(buffer_surface_);

  // Initialize the card cache
  initializeCardCache();
}

#ifdef USEOPENGL
void PyramidGame::setupOpenGLArea() {
  #ifdef __linux__
  // Create OpenGL rendering area
  gl_area_ = gtk_gl_area_new();
  gtk_widget_set_size_request(gl_area_, -1, -1);
  gtk_widget_set_can_focus(gl_area_, TRUE);
  
  // ✅ CRITICAL: Connect realize signal BEFORE showing window
  // This callback will be triggered when GL context is created
  g_signal_connect(G_OBJECT(gl_area_), "realize",
                  G_CALLBACK(onGLRealize), this);
  
  // ✅ Connect render signal for drawing each frame
  g_signal_connect(G_OBJECT(gl_area_), "render",
                  G_CALLBACK(onGLRender), this);
  
  // Enable event handling
  gtk_widget_add_events(gl_area_,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK |
      GDK_KEY_RELEASE_MASK);
  
  g_signal_connect(G_OBJECT(gl_area_), "button-press-event",
                  G_CALLBACK(onButtonPress), this);
  g_signal_connect(G_OBJECT(gl_area_), "button-release-event",
                  G_CALLBACK(onButtonRelease), this);
  g_signal_connect(G_OBJECT(gl_area_), "motion-notify-event",
                  G_CALLBACK(onMotionNotify), this);
  
  // Add size-allocate signal handler for resize events (CRITICAL FIX!)
  g_signal_connect(
      G_OBJECT(gl_area_), "size-allocate",
      G_CALLBACK(+[](GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
        PyramidGame *game = static_cast<PyramidGame *>(data);
        game->updateCardDimensions(allocation->width, allocation->height);
      }), this);
  // Set minimum size
  gtk_widget_set_size_request(
      gl_area_,
      BASE_CARD_WIDTH * 7 + BASE_CARD_SPACING * 8,
      BASE_CARD_HEIGHT * 2 + BASE_VERT_SPACING * 6);
  #endif
}
#endif

void PyramidGame::cleanupRenderingEngine() {
  switch (rendering_engine_) {
    case RenderingEngine::CAIRO:
      cleanupCardCache();
      cairo_initialized_ = false;
      break;

    case RenderingEngine::OPENGL:
      #ifdef __linux__
      cleanupOpenGLResources_gl();
      #endif
      opengl_initialized_ = false;
      break;

    default:
      break;
  }
}

std::string PyramidGame::getRenderingEngineName() const {
  switch (rendering_engine_) {
    case RenderingEngine::CAIRO:
      return "Cairo";
    case RenderingEngine::OPENGL:
      return "OpenGL";
    default:
      return "Unknown";
  }
}

void PyramidGame::printEngineInfo() {
  std::cout << "\n=== RENDERING ENGINE ===" << std::endl;
  std::cout << "Current: " << getRenderingEngineName() << std::endl;
  #ifdef __linux__
  std::cout << "Platform: Linux (OpenGL supported)" << std::endl;
  #else
  std::cout << "Platform: Windows/macOS (Cairo only)" << std::endl;
  #endif
  std::cout << "========================\n" << std::endl;
}

void PyramidGame::saveEnginePreference() {
  std::string config_file = settings_dir_ + "/graphics.ini";
  std::ofstream config(config_file);
  if (config.is_open()) {
    config << "[Graphics]\nengine=" 
           << (rendering_engine_ == RenderingEngine::CAIRO ? "cairo" : "opengl") << "\n";
    config.close();
  }
}

void PyramidGame::loadEnginePreference() {
  std::string config_file = settings_dir_ + "/graphics.ini";
  std::ifstream config(config_file);
  if (config.is_open()) {
    std::string line;
    while (std::getline(config, line)) {
      if (line.find("engine=") == 0) {
        std::string engine_name = line.substr(7);
        engine_name.erase(0, engine_name.find_first_not_of(" \t\r\n"));
        engine_name.erase(engine_name.find_last_not_of(" \t\r\n") + 1);
        
        #ifdef __linux__
        if (engine_name == "opengl") {
          rendering_engine_ = RenderingEngine::OPENGL;
        }
        #endif
      }
    }
    config.close();
  }
}

void PyramidGame::addEngineSelectionMenu(GtkWidget *menubar) {
  GtkWidget *graphics_menu = gtk_menu_new();
  GtkWidget *graphics_item = gtk_menu_item_new_with_label("Graphics");

  GtkWidget *cairo_item = gtk_menu_item_new_with_label("Use Cairo (CPU)");
  g_signal_connect(cairo_item, "activate",
                   G_CALLBACK(+[](GtkWidget *w, gpointer data) {
                     PyramidGame *game = static_cast<PyramidGame *>(data);
                     game->switchRenderingEngine(RenderingEngine::CAIRO);
                     game->refreshDisplay();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(graphics_menu), cairo_item);

  #ifdef __linux__
  GtkWidget *opengl_item = gtk_menu_item_new_with_label("Use OpenGL");
  g_signal_connect(opengl_item, "activate",
                   G_CALLBACK(+[](GtkWidget *w, gpointer data) {
                     PyramidGame *game = static_cast<PyramidGame *>(data);
                     game->switchRenderingEngine(RenderingEngine::OPENGL);
                     game->refreshDisplay();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(graphics_menu), opengl_item);
  #endif

  gtk_menu_item_set_submenu(GTK_MENU_ITEM(graphics_item), graphics_menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), graphics_item);
  gtk_widget_show_all(graphics_menu);
}

void PyramidGame::checkAndInitializeSound() {
  // Check if sound.zip file exists
  struct stat buffer;
  bool sound_file_exists = (stat(sounds_zip_path_.c_str(), &buffer) == 0);
  
  if (!sound_file_exists) {
    // Sound file doesn't exist - disable sound and show dialog
    sound_enabled_ = false;
    std::string message = "Sound file (sound.zip) was not found at:\n" + 
                          sounds_zip_path_ + 
                          "\n\nSound has been disabled. Game will continue without audio.";
    showErrorDialog("Sound File Missing", message);
  } else {
    // Sound file exists - initialize audio system
    initializeAudio();
#ifndef _WIN32
    usleep(100000);  // Unix/Linux usleep takes microseconds; timing issue
#endif
  }
}



void PyramidGame::run(int argc, char **argv) {
  gtk_init(&argc, &argv);
  setupWindow();
  initializeGame();  // Initialize game after GTK is ready and window exists
  setupGameArea();
  
  // Show all widgets AFTER game is fully initialized
  // This ensures GL context creation happens when game state is ready
  gtk_widget_show_all(window_);
  
  // NOW switch to OpenGL if that's the configured preference
  // This MUST happen AFTER gtk_widget_show_all() so the GL widget is properly realized
  #ifdef __linux__
  if (rendering_engine_ == RenderingEngine::OPENGL) {
    std::cout << "Switching to OpenGL mode (from graphics.ini) after widget realization..." << std::endl;
    gtk_stack_set_visible_child_name(GTK_STACK(rendering_stack_), "opengl");
    // Force processing of pending events to trigger realize callback
    while (gtk_events_pending()) {
      gtk_main_iteration();
    }
  }
  #endif
  
  gtk_main();
}

void PyramidGame::initializeGame() {
  // Check for engine switch request
  if (engine_switch_requested_) {
    switchRenderingEngine(requested_engine_);
    engine_switch_requested_ = false;
  }

  // FIX: Don't force CAIRO mode - respect the user's rendering engine choice
  // The rendering_engine_ is already set and should be preserved across game resets
  // Just ensure the appropriate renderer is marked as initialized
  if (rendering_engine_ == RenderingEngine::CAIRO) {
    cairo_initialized_ = true;
    opengl_initialized_ = false;
  } else {
     cairo_initialized_ = false;
     opengl_initialized_ = true;

  }

  if (current_game_mode_ == GameMode::STANDARD_PYRAMID) {
    // Original single-deck initialization
    try {
      // Try to find cards.zip in several common locations
#ifdef _WIN32
      const std::vector<std::string> paths = {getExecutableDir() + "\\cards.zip"};
#else
      const std::vector<std::string> paths = {"cards.zip"};
#endif
      bool loaded = false;
      for (const auto &path : paths) {
        try {
          deck_ = cardlib::Deck(path);
          deck_.removeJokers();
          loaded = true;
          break;
        } catch (const std::exception &e) {
          std::cerr << "Failed to load cards from " << path << ": " << e.what()
                    << std::endl;
        }
      }

      if (!loaded) {
        std::cerr << "Failed to find cards.zip in any of the expected locations.\n";
#ifdef _WIN32
        //showDirectoryStructureDialog(getExecutableDir());
#else
        //showDirectoryStructureDialog(".");
#endif
        showMissingFileDialog("cards.zip", "Card images are required to play this game.");
        exit(2); // Exit code 2: Missing required cards.zip
      }
      
      deck_.shuffle(current_seed_);

      // Clear all piles
      stock_.clear();
      waste_.clear();
      foundation_.clear();
      tableau_.clear();

      // Initialize foundation piles (4 empty piles for aces)
      foundation_.resize(4);

      // Initialize tableau (7 piles)
      tableau_.resize(7);

      // Deal cards
      deal();

    } catch (const std::exception &e) {
      std::cerr << "Fatal error during game initialization: " << e.what()
                << std::endl;
      showErrorDialog("Game Initialization Error", e.what());
      exit(1); // Exit code 1: General fatal error
    }
  } else {
    // Multi-deck initialization
    initializeMultiDeckGame();
  }
  
  // CRITICAL: Mark game as fully initialized
  // This prevents GL rendering from accessing uninitialized game state
  game_fully_initialized_ = true;
  std::cout << "✓ Game fully initialized - GL rendering now safe" << std::endl;
}

bool PyramidGame::isValidDragSource(int pile_index, int card_index) const {
  if (pile_index < 0)
    return false;

  // Can drag from waste pile only top card
  if (pile_index == 1) {
    return !waste_.empty() &&
           static_cast<size_t>(card_index) == waste_.size() - 1;
  }

  // Calculate maximum foundation index
  int max_foundation_index = 2 + foundation_.size() - 1;
  
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;

  // Can drag from foundation only top card
  if (pile_index >= 2 && pile_index <= max_foundation_index) {
    const auto &pile = foundation_[pile_index - 2];
    return !pile.empty() && static_cast<size_t>(card_index) == pile.size() - 1;
  }

  // Can drag from tableau if cards are face up
  if (pile_index >= first_tableau_index) {
    int tableau_idx = pile_index - first_tableau_index;
    if (tableau_idx >= 0 && static_cast<size_t>(tableau_idx) < tableau_.size()) {
      const auto &pile = tableau_[tableau_idx];
      return !pile.empty() && card_index >= 0 &&
             static_cast<size_t>(card_index) < pile.size() &&
             pile[card_index].face_up; // Make sure card is face up
    }
  }

  return false;
}

std::vector<cardlib::Card> &PyramidGame::getPileReference(int pile_index) {
  if (pile_index == 0)
    return stock_;
  if (pile_index == 1)
    return waste_;
    
  // Check foundation piles using foundation_.size() instead of hardcoded limit
  int max_foundation_index = 2 + foundation_.size() - 1;
  if (pile_index >= 2 && pile_index <= max_foundation_index)
    return foundation_[pile_index - 2];
    
  if (pile_index >= 6 && pile_index <= 12) {
    // We need to handle tableau differently or change the function signature
    throw std::runtime_error(
        "Cannot get reference to tableau pile - type mismatch");
  }
  throw std::out_of_range("Invalid pile index");
}

void PyramidGame::deal() {
  // Clear all piles first
  stock_.clear();
  waste_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset foundation and tableau
  foundation_.resize(4);
  tableau_.resize(7);

  // Deal to tableau - Pyramid Solitaire: all cards in pyramid are face up
  // tableau_[0] = 1 card (face up)
  // tableau_[1] = 2 cards (all face up)
  // tableau_[2] = 3 cards (all face up)
  // ... etc
  for (int i = 0; i < 7; i++) {
    // For each pile i, deal i+1 cards, all face up
    for (int j = 0; j <= i; j++) {
      if (auto card = deck_.drawCard()) {
        tableau_[i].emplace_back(*card, true); // All face up in pyramid
      }
    }
  }

  // Move remaining cards to stock (face down)
  while (auto card = deck_.drawCard()) {
    stock_.push_back(*card);
  }

#ifdef DEBUG
  std::cout << "Starting deal animation from deal()"
            << std::endl; // Debug output
#endif

  // Start the deal animation (call the correct version based on rendering engine)
  // FIX: Use conditional to call the right animation function for the active renderer
  std::cerr << "DEBUG: initializeGame() #1 - rendering_engine_ = " << (rendering_engine_ == RenderingEngine::OPENGL ? "OPENGL" : "CAIRO") << "\n";
    startDealAnimation();
}

void PyramidGame::flipTopTableauCard(int pile_index) {
  if (pile_index < 0 || pile_index >= static_cast<int>(tableau_.size())) {
    return;
  }

  auto &pile = tableau_[pile_index];
  if (!pile.empty() && !pile.back().face_up) {
    pile.back().face_up = true;
    // playSound(GameSoundEvent::CardFlip);
  }
}

GtkWidget *PyramidGame::createCardWidget(const cardlib::Card &card,
                                           bool face_up) {
  if (face_up) {
    if (auto img = deck_.getCardImage(card)) {
      // Create pixbuf from card image data
      GError *error = NULL;
      GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
      gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(),
                              &error);
      gdk_pixbuf_loader_close(loader, &error);

      GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
          pixbuf, CARD_WIDTH, CARD_HEIGHT, GDK_INTERP_BILINEAR);

      GtkWidget *image = gtk_image_new_from_pixbuf(scaled);
      g_object_unref(scaled);
      g_object_unref(loader);

      return image;
    }
  }

  // Create card back or placeholder
  GtkWidget *frame = gtk_frame_new(NULL);
  gtk_widget_set_size_request(frame, CARD_WIDTH, CARD_HEIGHT);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_OUT);
  return frame;
}

std::vector<cardlib::Card> PyramidGame::getTableauCardsAsCards(
    const std::vector<TableauCard> &tableau_cards, int start_index) {
  std::vector<cardlib::Card> cards;
  for (size_t i = start_index; i < tableau_cards.size(); i++) {
    if (tableau_cards[i].face_up) {
      cards.push_back(tableau_cards[i].card);
    }
  }
  return cards;
}

std::pair<int, int> PyramidGame::getPileAt(int x, int y) const {
  // DEBUG: Print complete foundation state
  std::cerr << "\n╔════════════════════════════════════════════════════════════════╗" << std::endl;
  std::cerr << "║ === GAME STATE SNAPSHOT ===" << std::endl;
  std::cerr << "║ Stock: " << stock_.size() << " cards" << std::endl;
  std::cerr << "║ Waste: " << waste_.size() << " cards";
  if (!waste_.empty()) {
    const auto& wc = waste_.back();
    std::cerr << " | Top: " << static_cast<int>(wc.rank) << " of " << static_cast<int>(wc.suit);
  }
  std::cerr << std::endl;
  
  std::cerr << "║ FOUNDATION PILES:" << std::endl;
  for (size_t i = 0; i < foundation_.size(); i++) {
    std::cerr << "║   [" << i << "]: " << foundation_[i].size() << " cards";
    if (!foundation_[i].empty()) {
      const auto& fc = foundation_[i].back();
      std::cerr << " | Top: Rank=" << static_cast<int>(fc.rank) << ", Suit=" << static_cast<int>(fc.suit);
    }
    std::cerr << std::endl;
  }
  std::cerr << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
  
  std::cerr << "\n=== getPileAt DEBUG ===" << std::endl;
  std::cerr << "Click position: (" << x << ", " << y << ")" << std::endl;
  std::cerr << "Card dimensions: " << current_card_width_ << "x" << current_card_height_ << std::endl;
  std::cerr << "Card spacing: " << current_card_spacing_ << ", Vert spacing: " << current_vert_spacing_ << std::endl;
  
  // Check stock pile
  if (x >= current_card_spacing_ &&
      x <= current_card_spacing_ + current_card_width_ &&
      y >= current_card_spacing_ &&
      y <= current_card_spacing_ + current_card_height_) {
    std::cerr << "MATCH: Stock pile" << std::endl;
    return {0, stock_.empty() ? -1 : 0};
  }

  // Check waste pile
  if (x >= 2 * current_card_spacing_ + current_card_width_ &&
      x <= 2 * current_card_spacing_ + 2 * current_card_width_ &&
      y >= current_card_spacing_ &&
      y <= current_card_spacing_ + current_card_height_) {
    std::cerr << "MATCH: Waste pile" << std::endl;
    return {1, waste_.empty() ? -1 : static_cast<int>(waste_.size() - 1)};
  }

  // Check foundation piles
  std::cerr << "\nChecking foundation piles (count: " << foundation_.size() << ")" << std::endl;
  int foundation_x = 3 * (current_card_width_ + current_card_spacing_);
  for (int i = 0; i < foundation_.size(); i++) {
    std::cerr << "  Foundation[" << i << "]: x range [" << foundation_x << ", " 
              << (foundation_x + current_card_width_) << "]";
    std::cerr << ", y range [" << current_card_spacing_ << ", " 
              << (current_card_spacing_ + current_card_height_) << "]";
    std::cerr << ", size: " << foundation_[i].size();
    
    if (x >= foundation_x && x <= foundation_x + current_card_width_ &&
        y >= current_card_spacing_ &&
        y <= current_card_spacing_ + current_card_height_) {
      int card_idx = foundation_[i].empty() ? -1 : static_cast<int>(foundation_[i].size() - 1);
      std::cerr << " <- MATCH!" << std::endl;
      std::cerr << "Returning: pile_index=" << (2 + i) << ", card_index=" << card_idx << std::endl;
      
      // Debug: print the card if not empty
      if (card_idx >= 0 && card_idx < static_cast<int>(foundation_[i].size())) {
        const auto &card = foundation_[i][card_idx];
        std::cerr << "  Card at index " << card_idx << ": Rank=" << static_cast<int>(card.rank) 
                  << ", Suit=" << static_cast<int>(card.suit);
        std::cerr << " (" << cardlib::rankToString(card.rank) << " of " << cardlib::suitToString(card.suit) << ")" << std::endl;
      }
      return {2 + i, card_idx};
    }
    std::cerr << std::endl;
    
    foundation_x += current_card_width_ + current_card_spacing_;
  }

  // Calculate first tableau index
  int first_tableau_index = 2 + foundation_.size();
  std::cerr << "\nChecking tableau piles (first_tableau_index=" << first_tableau_index << ")" << std::endl;
  
  // Check pyramid piles
  // tableau_[0] = row 1 (1 card)
  // tableau_[1] = row 2 (2 cards staggered)
  // ...
  // tableau_[6] = row 7 (7 cards staggered)
  
  const int base_y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
  
  // Get actual window width instead of hardcoded value
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);
  int screen_width = allocation.width;
  
  const int HORIZ_SPACING = current_card_width_ + 15;    // Card width plus 15 pixel gap
  const int VERT_OVERLAP = current_card_height_ / 2;     // Half card height between rows
  
  for (int row = 0; row < 7; row++) {
    const auto &pile = tableau_[row];
    int num_cards_in_row = row + 1;
    
    // Calculate row dimensions
    int row_width = current_card_width_ + (num_cards_in_row - 1) * HORIZ_SPACING;
    int row_start_x = (screen_width - row_width) / 2;
    int row_y = base_y + row * VERT_OVERLAP;
    
    std::cerr << "  Row[" << row << "]: " << pile.size() << " cards, y=[" << row_y << ", " 
              << (row_y + current_card_height_) << "], row_start_x=" << row_start_x << std::endl;
    
    // Check each card in this row, from last to first (top-most cards first)
    for (int card_idx = static_cast<int>(pile.size()) - 1; card_idx >= 0; card_idx--) {
      int card_x = row_start_x + (card_idx * HORIZ_SPACING);
      
      const auto &tableau_card = pile[card_idx];
      const auto &card = tableau_card.card;
      std::cerr << "    [" << card_idx << "]: ";
      std::cerr << "Rank=" << static_cast<int>(card.rank) << "(" << cardlib::rankToString(card.rank) << ") ";
      std::cerr << "Suit=" << static_cast<int>(card.suit) << "(" << cardlib::suitToString(card.suit) << ") ";
      std::cerr << "face_up=" << (tableau_card.face_up ? "YES" : "NO") << " | ";
      std::cerr << "x=[" << card_x << ", " << (card_x + current_card_width_) << "]";
      
      // Check if click is on this card
      if (x >= card_x && x <= card_x + current_card_width_ &&
          y >= row_y && y <= row_y + current_card_height_) {
        if (tableau_card.face_up) {
          std::cerr << " <- MATCH! ✓" << std::endl;
          std::cerr << ">>> RETURNING: pile_index=" << (first_tableau_index + row) << ", card_idx=" << card_idx;
          std::cerr << " | CARD DATA: Rank=" << static_cast<int>(card.rank) << ", Suit=" << static_cast<int>(card.suit);
          std::cerr << " (" << cardlib::rankToString(card.rank) << " of " << cardlib::suitToString(card.suit) << ")" << std::endl;
          return {first_tableau_index + row, card_idx};
        } else {
          std::cerr << " <- HIT but FACE DOWN (skipped)" << std::endl;
        }
      } else {
        std::cerr << std::endl;
      }
    }
    
    // Check empty row area if empty
    if (pile.empty()) {
      int card_x = row_start_x;
      std::cerr << "    [EMPTY]: x=[" << card_x << ", " << (card_x + current_card_width_) << "]";
      if (x >= card_x && x <= card_x + current_card_width_ &&
          y >= row_y && y <= row_y + current_card_height_) {
        std::cerr << " <- MATCH! ✓" << std::endl;
        return {first_tableau_index + row, -1};
      }
      std::cerr << std::endl;
    }
  }

  std::cerr << "NO MATCH found for click at (" << x << ", " << y << ")" << std::endl;
  return {-1, -1};;
}

bool PyramidGame::canMoveToPile(const std::vector<cardlib::Card> &cards,
                                  const std::vector<cardlib::Card> &target,
                                  bool is_foundation) const {

  if (cards.empty())
    return false;

  const auto &card1 = cards[0];

  // Pyramid Solitaire: Cards are paired and removed, not moved to piles
  // Only single cards can be paired
  if (cards.size() != 1) {
    return false;
  }

  // King alone (sum to 13 by itself)
  if (target.empty()) {
    return static_cast<int>(card1.rank) == static_cast<int>(cardlib::Rank::KING);
  }

  // Must have exactly one target card to pair with
  if (target.size() != 1) {
    return false;
  }

  const auto &card2 = target[0];

  // Calculate rank values (A=1, 2=2, ..., Q=12, K=13)
  // cardlib uses 0-based ranks (A=0 to K=12), so add 1 to get face values
  int rank1 = static_cast<int>(card1.rank) + 1;
  int rank2 = static_cast<int>(card2.rank) + 1;

  // Cards pair if their ranks sum to 13
  return (rank1 + rank2) == 13;
}

bool PyramidGame::canMoveToFoundation(const cardlib::Card &card,
                                        int foundation_index) const {
  const auto &pile = foundation_[foundation_index];

  std::cerr << "[canMoveToFoundation] Card: " << cardlib::rankToString(card.rank) << " of " 
            << cardlib::suitToString(card.suit) << ", Foundation[" << foundation_index << "] size=" << pile.size();

  if (pile.empty()) {
    bool result = card.rank == cardlib::Rank::ACE;
    std::cerr << " -> Empty foundation, need ACE: " << (result ? "YES" : "NO") << std::endl;
    return result;
  }

  const auto &top_card = pile.back();
  bool suit_match = card.suit == top_card.suit;
  bool rank_match = static_cast<int>(card.rank) == static_cast<int>(top_card.rank) + 1;
  bool result = suit_match && rank_match;
  
  std::cerr << " -> Top: " << cardlib::rankToString(top_card.rank) << " of " 
            << cardlib::suitToString(top_card.suit);
  std::cerr << " [Suit:" << (suit_match ? "✓" : "✗") << " Rank:" << (rank_match ? "✓" : "✗") << "] = " 
            << (result ? "CAN MOVE" : "CANNOT") << std::endl;
  return result;
}

void PyramidGame::moveCards(std::vector<cardlib::Card> &from,
                              std::vector<cardlib::Card> &to, size_t count) {
  if (count > from.size())
    return;

  to.insert(to.end(), from.end() - count, from.end());

  from.erase(from.end() - count, from.end());
}

void PyramidGame::switchGameMode(GameMode mode) {
  if (mode == current_game_mode_)
    return;

  if (win_animation_active_) {
    stopWinAnimation();
  }
    
  // Update the game mode
  current_game_mode_ = mode;

  // Start a new game with the selected mode
  if (mode == GameMode::STANDARD_PYRAMID) {
    initializeGame(); // Use the existing single-deck initialization
  } else {
    initializeMultiDeckGame(); // Use the new multi-deck initialization
  }
  
  // Get current window dimensions to update card scaling
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);
  updateCardDimensions(allocation.width, allocation.height);
  updateWindowTitle();
  refreshDisplay();
}

void PyramidGame::initializeMultiDeckGame() {
  try {
    // Determine number of decks based on mode
    size_t num_decks = (current_game_mode_ == GameMode::DOUBLE_PYRAMID) ? 2 : 3;
    
    // Try to find cards.zip in several common locations
    const std::vector<std::string> paths = {"cards.zip"};

    bool loaded = false;
    for (const auto &path : paths) {
      try {
        // Use MultiDeck instead of Deck
        multi_deck_ = cardlib::MultiDeck(num_decks, path);
        
        // Remove jokers from all decks
        for (size_t i = 0; i < num_decks; i++) {
          multi_deck_.getDeck(i).removeJokers();
        }
        
        loaded = true;
        break;
      } catch (const std::exception &e) {
        std::cerr << "Failed to load cards from " << path << ": " << e.what()
                  << std::endl;
      }
    }

    if (!loaded) {
      throw std::runtime_error("Could not find cards.zip in any search path");
    }
    
    multi_deck_.shuffle(current_seed_);

    // Clear all piles
    stock_.clear();
    waste_.clear();
    foundation_.clear();
    tableau_.clear();

    // For multiple decks, increase the number of foundation piles
    // Each suit appears multiple times (once per deck)
    foundation_.resize(4 * num_decks);

    // Keep tableau at 7 piles for simplicity
    tableau_.resize(7);

    // Deal cards using the multi-deck deal method
    dealMultiDeck();

  } catch (const std::exception &e) {
    std::cerr << "Fatal error during game initialization: " << e.what()
              << std::endl;
    exit(1);
  }
}


void PyramidGame::dealMultiDeck() {
  // Clear all piles first
  stock_.clear();
  waste_.clear();
  
  // Deal to tableau - Pyramid Solitaire: all cards in pyramid are face up
  for (int i = 0; i < 7; i++) {
    // For each pile i, deal i+1 cards ALL face up (no face-down cards in Pyramid)
    for (int j = 0; j <= i; j++) {
      if (auto card = multi_deck_.drawCard()) {
        tableau_[i].emplace_back(*card, true);  // ALL face up
      }
    }
  }

  // Move remaining cards to stock (face down)
  while (auto card = multi_deck_.drawCard()) {
    stock_.push_back(*card);
  }

  // Start the deal animation (call the correct version based on rendering engine)
  // FIX: Use conditional to call the right animation function for the active renderer
  std::cerr << "DEBUG: initializeGame() #2 - rendering_engine_ = " << (rendering_engine_ == RenderingEngine::OPENGL ? "OPENGL" : "CAIRO") << "\n";
  startDealAnimation();
}

bool PyramidGame::checkWinCondition() const {
  // Pyramid Solitaire: Win when all cards from the pyramid and waste are moved to discard
  // The pyramid and waste must be empty (all cards discarded as matches)
  
  // Check if all pyramid cards are gone
  for (const auto &pile : tableau_) {
    if (!pile.empty()) {
      return false;  // Still have cards in the pyramid
    }
  }
  
  // Check if waste pile is empty
  if (!waste_.empty()) {
    return false;  // Still have cards in waste
  }

  // All pyramid and waste cards have been matched and discarded = WIN!
  return true;
}

// Function to refresh the display
void PyramidGame::refreshDisplay() {
  // FIX: Refresh the correct widget based on the active rendering engine
  if (rendering_engine_ == RenderingEngine::OPENGL) {
    if (gl_area_) {
      gtk_widget_queue_draw(gl_area_);
    }
  } else {
    if (game_area_) {
      gtk_widget_queue_draw(game_area_);
    }
  }
}

int main(int argc, char **argv) {
  PyramidGame game;
  game.run(argc, argv);
  return 0;
}

void PyramidGame::setupWindow() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  //gtk_window_set_title(GTK_WINDOW(window_), "Solitaire");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit),
                   NULL);
  updateWindowTitle();
  gtk_widget_add_events(window_, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window_), "key-press-event", G_CALLBACK(onKeyPress),
                   this);

  // Make sure the window is realized before calculating scale
  gtk_widget_realize(window_);
  
  // Check if sound.zip exists and initialize sound system
  checkAndInitializeSound();
    
  // Now get the initial dimensions with correct scale factor
  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);
  updateCardDimensions(allocation.width, allocation.height);

  // Create vertical box
  vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window_), vbox_);

  // Setup menu bar
  setupMenuBar();
}

void PyramidGame::setupGameArea() {
  // Create Cairo rendering area
  setupCairoArea();
  
  // Create OpenGL rendering area
#ifdef USEOPENGL
  setupOpenGLArea();
#endif
  
  // Create GtkStack to switch between rendering engines
  rendering_stack_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(rendering_stack_), 
                                GTK_STACK_TRANSITION_TYPE_NONE);
  
  // Add both widgets to stack
  gtk_stack_add_named(GTK_STACK(rendering_stack_), game_area_, "cairo");
  #ifdef __linux__
  gtk_stack_add_named(GTK_STACK(rendering_stack_), gl_area_, "opengl");
  #endif
  
  // Always start with Cairo initially - will switch to OpenGL after show_all()
  // This ensures proper GTK widget realization before GL context creation
  gtk_stack_set_visible_child_name(GTK_STACK(rendering_stack_), "cairo");
  
  // Pack stack into main window
  gtk_box_pack_start(GTK_BOX(vbox_), rendering_stack_, TRUE, TRUE, 0);
  
  // NOTE: After gtk_widget_show_all() is called from run(), we will switch to
  // OpenGL if that's the configured preference (in run() method)
}

void PyramidGame::setupMenuBar() {
  GtkWidget *menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(vbox_), menubar, FALSE, FALSE, 0);

  // ==================== GAME MENU ====================
  GtkWidget *gameMenu = gtk_menu_new();
  GtkWidget *gameMenuItem = gtk_menu_item_new_with_mnemonic("_Game");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

  // New Game
  GtkWidget *newGameItem = gtk_menu_item_new_with_mnemonic("_New Game (Ctrl+N)");
  g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

  // Restart Game (same seed)
  GtkWidget *restartGameItem = gtk_menu_item_new_with_label("Restart Game");
  g_signal_connect(G_OBJECT(restartGameItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<PyramidGame *>(data)->restartGame();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), restartGameItem);

  // Enter Seed
  GtkWidget *seedItem = gtk_menu_item_new_with_label("Enter Seed...");
  g_signal_connect(G_OBJECT(seedItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<PyramidGame *>(data)->promptForSeed();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), seedItem);

  // Auto Finish
  GtkWidget *autoFinishItem = gtk_menu_item_new_with_mnemonic("_Auto Finish (F)");
  g_signal_connect(G_OBJECT(autoFinishItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<PyramidGame *>(data)->autoFinishGame();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), autoFinishItem);

GtkWidget *gameModeItem = gtk_menu_item_new_with_mnemonic("_Game Mode");
GtkWidget *gameModeMenu = gtk_menu_new();
gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameModeItem), gameModeMenu);

// Standard Pyramid Solitaire option (1 deck)
GtkWidget *standardItem = gtk_radio_menu_item_new_with_mnemonic(NULL, "One Deck");
GSList *modeGroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(standardItem));
g_signal_connect(
    G_OBJECT(standardItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<PyramidGame *>(data)->switchGameMode(PyramidGame::GameMode::STANDARD_PYRAMID);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), standardItem);

// Double Pyramid Solitaire option (2 decks)
GtkWidget *doubleItem = gtk_radio_menu_item_new_with_mnemonic(modeGroup, "Two Decks");
g_signal_connect(
    G_OBJECT(doubleItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<PyramidGame *>(data)->switchGameMode(PyramidGame::GameMode::DOUBLE_PYRAMID);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), doubleItem);

// Triple Pyramid Solitaire option (3 decks)
GtkWidget *tripleItem = gtk_radio_menu_item_new_with_mnemonic(modeGroup, "Three Decks");
g_signal_connect(
    G_OBJECT(tripleItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<PyramidGame *>(data)->switchGameMode(PyramidGame::GameMode::TRIPLE_PYRAMID);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), tripleItem);

// Set initial state based on current mode
gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(
        current_game_mode_ == GameMode::STANDARD_PYRAMID ? standardItem :
        current_game_mode_ == GameMode::DOUBLE_PYRAMID ? doubleItem : tripleItem),
    TRUE);

// Add the game mode submenu to the options menu
gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), gameModeItem);


  // Add separator before Quit
  GtkWidget *sep = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep);

  // Quit
  GtkWidget *quitItem = gtk_menu_item_new_with_mnemonic("_Quit (Ctrl+Q)");
  g_signal_connect(G_OBJECT(quitItem), "activate", G_CALLBACK(onQuit), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), quitItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), gameMenuItem);

  // ==================== GRAPHICS MENU ====================
  addEngineSelectionMenu(menubar);

  // ==================== OPTIONS MENU ====================
  GtkWidget *optionsMenu = gtk_menu_new();
  GtkWidget *optionsMenuItem = gtk_menu_item_new_with_mnemonic("_Options");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(optionsMenuItem), optionsMenu);

  // Draw Mode menu removed - Pyramid Solitaire only uses Draw One mode
  
  // Card Back menu
  GtkWidget *cardBackMenu = gtk_menu_new();
  GtkWidget *cardBackItem = gtk_menu_item_new_with_mnemonic("_Card Back");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(cardBackItem), cardBackMenu);

  // Select custom back option
  GtkWidget *selectBackItem = gtk_menu_item_new_with_mnemonic("_Select Custom Back");
  g_signal_connect(
      G_OBJECT(selectBackItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        PyramidGame *game = static_cast<PyramidGame *>(data);

        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Select Card Back", GTK_WINDOW(game->window_),
            GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open", GTK_RESPONSE_ACCEPT, NULL);

        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Image Files");
        gtk_file_filter_add_pattern(filter, "*.png");
        gtk_file_filter_add_pattern(filter, "*.jpg");
        gtk_file_filter_add_pattern(filter, "*.jpeg");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
          char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
          if (game->setCustomCardBack(filename)) {
            game->refreshCardCache();
            game->refreshDisplay();
          } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to load image file");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
          }
          g_free(filename);
        }
        gtk_widget_destroy(dialog);
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(cardBackMenu), selectBackItem);

  // Reset to default back option
  GtkWidget *resetBackItem = gtk_menu_item_new_with_mnemonic("_Reset to Default Back");
  g_signal_connect(G_OBJECT(resetBackItem), "activate",
                   G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     PyramidGame *game = static_cast<PyramidGame *>(data);
                     game->resetToDefaultBack();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(cardBackMenu), resetBackItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), cardBackItem);

  // Load Deck option
  GtkWidget *loadDeckItem = gtk_menu_item_new_with_mnemonic("_Load Deck (Ctrl+L)");
  g_signal_connect(
      G_OBJECT(loadDeckItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        PyramidGame *game = static_cast<PyramidGame *>(data);

        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Load Deck", GTK_WINDOW(game->window_),
            GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open", GTK_RESPONSE_ACCEPT, NULL);

        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Card Deck Files (*.zip)");
        gtk_file_filter_add_pattern(filter, "*.zip");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
          char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
          if (game->loadDeck(filename)) {
            game->refreshDisplay();
          }
          g_free(filename);
        }

        gtk_widget_destroy(dialog);
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), loadDeckItem);

  // Add fullscreen toggle
  GtkWidget *fullscreenItem = gtk_menu_item_new_with_mnemonic("Toggle _Fullscreen (F11)");
  g_signal_connect(G_OBJECT(fullscreenItem), "activate", G_CALLBACK(onToggleFullscreen), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), fullscreenItem);

  // Add sound toggle
  GtkWidget *soundItem = gtk_check_menu_item_new_with_mnemonic("_Sound (Ctrl+S)");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(soundItem), sound_enabled_);
  g_signal_connect(G_OBJECT(soundItem), "toggled",
                   G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     PyramidGame *game = static_cast<PyramidGame *>(data);
                     game->sound_enabled_ = gtk_check_menu_item_get_active(
                         GTK_CHECK_MENU_ITEM(widget));
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), soundItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), optionsMenuItem);

  // ==================== HELP MENU ====================
  GtkWidget *helpMenu = gtk_menu_new();
  GtkWidget *helpMenuItem = gtk_menu_item_new_with_mnemonic("_Help");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMenuItem), helpMenu);

  // How To Play
  GtkWidget *howToPlayItem = gtk_menu_item_new_with_mnemonic("_How To Play");
  g_signal_connect(G_OBJECT(howToPlayItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    PyramidGame *game = static_cast<PyramidGame *>(data);
                    game->showHowToPlay();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), howToPlayItem);

  // Keyboard Shortcuts
  GtkWidget *shortcutsItem = gtk_menu_item_new_with_mnemonic("_Keyboard Shortcuts");
  g_signal_connect(G_OBJECT(shortcutsItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    PyramidGame *game = static_cast<PyramidGame *>(data);
                    game->showKeyboardShortcuts();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), shortcutsItem);

  // About
  GtkWidget *aboutItem = gtk_menu_item_new_with_mnemonic("_About (Ctrl+H)");
  g_signal_connect(G_OBJECT(aboutItem), "activate", G_CALLBACK(onAbout), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMenuItem);

#ifdef DEBUG
  // Debug Menu
  GtkWidget *debugMenu = gtk_menu_new();
  GtkWidget *debugMenuItem = gtk_menu_item_new_with_mnemonic("_Debug");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(debugMenuItem), debugMenu);

  GtkWidget *testLayoutItem = gtk_menu_item_new_with_label("Test Layout");
  g_signal_connect(G_OBJECT(testLayoutItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    PyramidGame *game = static_cast<PyramidGame *>(data);
                    game->dealTestLayout();
                    game->refreshDisplay();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(debugMenu), testLayoutItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), debugMenuItem);
#endif
}

void PyramidGame::onNewGame(GtkWidget *widget, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
    
  // Check if win animation is active
  if (game->win_animation_active_) {
    game->stopWinAnimation();
  }

  game->current_seed_ = rand();
  game->initializeGame();
  game->updateWindowTitle();
  game->refreshDisplay();
}

void PyramidGame::restartGame() {
  // Check if win animation is active
  if (win_animation_active_) {
    stopWinAnimation();
  }

  // Keep the current seed and restart the game
  initializeGame();
  refreshDisplay();
}

void PyramidGame::onQuit(GtkWidget *widget, gpointer data) {
  gtk_main_quit();
}

void PyramidGame::updateWindowTitle() {
  if (window_) {
    std::string title = "Pyramid Solitaire - Seed: " + std::to_string(current_seed_);
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
  }
}

void PyramidGame::onAbout(GtkWidget * /* widget */, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  // Create custom dialog instead of about dialog for more control
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "About Pyramid Solitaire", GTK_WINDOW(game->window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                  GTK_DIALOG_DESTROY_WITH_PARENT),
      "OK", GTK_RESPONSE_OK, NULL);

  // Set minimum dialog size
  gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

  // Create and configure the content area
  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 24);
  gtk_widget_set_margin_bottom(content_area, 12);

  // Add program name with larger font
  GtkWidget *name_label = gtk_label_new(NULL);
  const char *name_markup =
      "<span size='x-large' weight='bold'>Pyramid Solitaire</span>";
  gtk_label_set_markup(GTK_LABEL(name_label), name_markup);
  gtk_container_add(GTK_CONTAINER(content_area), name_label);

  // Add version
  GtkWidget *version_label = gtk_label_new("Version 1.0");
  gtk_container_add(GTK_CONTAINER(content_area), version_label);
  gtk_widget_set_margin_bottom(version_label, 12);

  // Add game instructions in a scrolled window
  GtkWidget *instructions_text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(instructions_text), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(instructions_text), 12);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(instructions_text), 12);

  GtkTextBuffer *buffer =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(instructions_text));
  const char *instructions =
      "How to Play Pyramid Solitaire:\n\n"
      "Objective:\n"
      "Remove all cards by pairing cards that sum to 13.\n\n"
      "Game Setup:\n"
      "- Cards are laid out in a pyramid pattern\n"
      "- The pyramid has 7 rows with one card in the top row\n"
      "- Each row has one more card than the row above it\n"
      "- Cards below the pyramid form the draw pile\n\n"
      "Rules:\n"
      "1. You can only remove cards that are completely exposed (not covered by other cards)\n"
      "2. Remove pairs of cards that add up to 13:\n"
      "   - Ace = 1, Jack = 11, Queen = 12, King = 13 (King can be removed alone)\n"
      "   - For example: 6 + 7 = 13, 5 + 8 = 13, etc.\n"
      "3. Click cards in the pyramid to match pairs\n"
      "4. Use cards from the draw pile to match with exposed cards\n"
      "5. Remove all cards to win the game\n\n"
      "Controls:\n"
      "- Left-click cards to select them for matching\n"
      "- Click the draw pile to reveal new cards\n\n"
      "Keyboard Controls:\n"
      "- Arrow keys to navigate\n"
      "- Enter to select cards\n"
      "- F11 to toggle fullscreen mode\n"
      "- Ctrl+N for a new game\n"
      "- Ctrl+Q to quit\n"
      "- Ctrl+H for help\n\n"
      "Written by Jason Hall\n"
      "Licensed under the MIT License\n"
      "https://github.com/jasonbrianhall/pyramid";

  gtk_text_buffer_set_text(buffer, instructions, -1);

  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  // Set the size of the scrolled window to be larger
  gtk_widget_set_size_request(scrolled_window, 550, 400);

  gtk_container_add(GTK_CONTAINER(scrolled_window), instructions_text);
  gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

  // Show all widgets before running the dialog
  gtk_widget_show_all(dialog);

  // Run dialog and get the result
  gint result = gtk_dialog_run(GTK_DIALOG(dialog));

  // Check for secret layout (Ctrl key pressed during dialog)
  if (result == GTK_RESPONSE_OK) {
    GdkModifierType modifiers;
    gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(dialog)), NULL,
                           NULL, &modifiers);

    if (modifiers & GDK_CONTROL_MASK) {
      // Activate test layout
      game->dealTestLayout();
      game->refreshDisplay();
    }
  }

  // Destroy dialog
  gtk_widget_destroy(dialog);
}

void PyramidGame::dealTestLayout() {
  // Clear all piles
  if (win_animation_active_) {
    stopWinAnimation();
  }

  stock_.clear();
  waste_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset foundation and tableau
  // Number of foundation piles depends on the game mode
  size_t num_decks = 1;
  if (current_game_mode_ == GameMode::DOUBLE_PYRAMID) {
    num_decks = 2;
  } else if (current_game_mode_ == GameMode::TRIPLE_PYRAMID) {
    num_decks = 3;
  }

  // Resize foundation based on number of decks (4 foundations per deck)
  foundation_.resize(4 * num_decks);
  tableau_.resize(7);  // Always 7 tableau piles

  // Set up each suit in order in the tableau
  std::vector<cardlib::Card> all_cards;

  // Create cards for each deck
  for (size_t deck = 0; deck < num_decks; deck++) {
    for (int suit = 0; suit < 4; suit++) {
      // Add 13 cards of this suit to a vector in reverse order (King to Ace)
      for (int rank = static_cast<int>(cardlib::Rank::KING);
           rank >= static_cast<int>(cardlib::Rank::ACE); rank--) {
        all_cards.emplace_back(static_cast<cardlib::Suit>(suit),
                              static_cast<cardlib::Rank>(rank));
      }
    }
  }

  // Distribute the cards to tableau
  for (size_t i = 0; i < all_cards.size(); i++) {
    tableau_[i % 7].emplace_back(all_cards[i], true);  // All cards face up
  }
}

void PyramidGame::initializeSettingsDir() {
#ifdef _WIN32
    char app_data[MAX_PATH];
    HRESULT hr = SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, app_data);
    if (hr != S_OK) {
        std::cerr << "SHGetFolderPathA failed with code: " << hr << std::endl;
        settings_dir_ = "./";
        return;
    }
    std::cerr << "AppData path: " << app_data << std::endl;
    settings_dir_ = std::string(app_data) + "\\Pyramid Solitaire";
    std::cerr << "Settings dir: " << settings_dir_ << std::endl;
    if (!CreateDirectoryA(settings_dir_.c_str(), NULL)) {
        std::cerr << "CreateDirectoryA failed, error: " << GetLastError() << std::endl;
    }
#else
    const char *home = getenv("HOME");
    if (!home) {
        settings_dir_ = "./";
        return;
    }
    settings_dir_ = std::string(home) + "/.pyramid-solitaire";
    mkdir(settings_dir_.c_str(), 0755);
#endif
}

bool PyramidGame::loadSettings() {
  if (settings_dir_.empty()) {
    std::cerr << "Settings directory is empty" << std::endl;
    return false;
  }

  std::string settings_file = settings_dir_ +
#ifdef _WIN32
                              "\\settings.txt"
#else
                              "/settings.txt"
#endif
      ;

  std::cerr << "Attempting to load settings from: " << settings_file << std::endl;
  
  std::ifstream file(settings_file);
  if (!file) {
    std::cerr << "Failed to open settings file" << std::endl;
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.substr(0, 10) == "card_back=") {
      custom_back_path_ = line.substr(10);
      std::cerr << "Loaded custom back path: " << custom_back_path_ << std::endl;
    }
  }

  return true; // Return true if we successfully read the file, even if no custom back was found
}

void PyramidGame::saveSettings() {
  if (settings_dir_.empty()) {
    return;
  }

  std::string settings_file = settings_dir_ +
#ifdef _WIN32
                              "\\settings.txt"
#else
                              "/settings.txt"
#endif
      ;

  std::ofstream file(settings_file);
  if (!file) {
    std::cerr << "Could not save settings" << std::endl;
    return;
  }

  if (!custom_back_path_.empty()) {
    file << "card_back=" << custom_back_path_ << std::endl;
  }
}

bool PyramidGame::setCustomCardBack(const std::string &path) {

  // First read the entire file into memory
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }

  // Store original path
  std::string old_path = custom_back_path_;

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    return false;
  }

  // Now create pixbuf from memory
  GError *error = nullptr;
  GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

  if (!gdk_pixbuf_loader_write(loader, (const guchar *)buffer.data(), size,
                               &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(loader);
    return false;
  }

  if (!gdk_pixbuf_loader_close(loader, &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(loader);
    return false;
  }

  GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
  if (!pixbuf) {
    g_object_unref(loader);
    return false;
  }

  g_object_unref(loader); // This will also unreference the pixbuf

  try {
    custom_back_path_ = path;

    saveSettings();

    return true;

  } catch (const std::exception &e) {
    custom_back_path_ = old_path; // Restore old path
    return false;
  }
}

bool PyramidGame::loadDeck(const std::string &path) {
  try {
    // Load the new deck first to validate it
    cardlib::Deck new_deck(path);
    new_deck.removeJokers();

    // If we got here, the deck loaded successfully
    cleanupResources();
    deck_ = std::move(new_deck);
    refreshDisplay();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to load deck from " << path << ": " << e.what()
              << std::endl;
    GtkWidget *error_dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK, "Failed to load deck: %s", e.what());
    gtk_dialog_run(GTK_DIALOG(error_dialog));
    gtk_widget_destroy(error_dialog);
    return false;
  }
}

void PyramidGame::resetToDefaultBack() {
  clearCustomBack();
  refreshCardCache();
  refreshDisplay();
}

void PyramidGame::onToggleFullscreen(GtkWidget *widget, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->toggleFullscreen();
}

void PyramidGame::updateCardDimensions(int window_width, int window_height) {
  double scale = getScaleFactor(window_width, window_height);

  // Update current dimensions
  current_card_width_ = static_cast<int>(BASE_CARD_WIDTH * scale);
  current_card_height_ = static_cast<int>(BASE_CARD_HEIGHT * scale);
  current_card_spacing_ = static_cast<int>(BASE_CARD_SPACING * scale);
  current_vert_spacing_ = static_cast<int>(BASE_VERT_SPACING * scale);

  // Ensure minimum sizes
  current_card_width_ = std::max(current_card_width_, 60);
  current_card_height_ = std::max(current_card_height_, 87);
  current_card_spacing_ = std::max(current_card_spacing_, 10);
  current_vert_spacing_ = std::max(current_vert_spacing_, 15);

  // Ensure cards don't overlap
  if (current_vert_spacing_ < current_card_height_ / 4) {
    current_vert_spacing_ = current_card_height_ / 4;
  }

  // Reinitialize card cache with new dimensions
  initializeCardCache();
}

double PyramidGame::getScaleFactor(int window_width, int window_height) const {
  // Get the display scale factor (1.0 for 100%, 2.0 for 200%, etc.)
  double display_scale = 1.0;
  if (window_) {
    GdkWindow *gdk_window = gtk_widget_get_window(window_);
    if (gdk_window) {
      display_scale = gdk_window_get_scale_factor(gdk_window);
    }
  }
  
  // Adjust window dimensions to logical pixels (divide by display scale)
  int logical_width = static_cast<int>(window_width / display_scale);
  int logical_height = static_cast<int>(window_height / display_scale);
  
  // Define optimal widths for each game mode based on testing
  const int OPTIMAL_WIDTH_STANDARD = 800;
  const int OPTIMAL_WIDTH_DOUBLE = 1300;
  const int OPTIMAL_WIDTH_TRIPLE = 1800;
  
  // Select the optimal width based on current game mode
  int optimal_width;
  switch (current_game_mode_) {
    case GameMode::DOUBLE_PYRAMID:
      optimal_width = OPTIMAL_WIDTH_DOUBLE;
      break;
    case GameMode::TRIPLE_PYRAMID:
      optimal_width = OPTIMAL_WIDTH_TRIPLE;
      break;
    case GameMode::STANDARD_PYRAMID:
    default:
      optimal_width = OPTIMAL_WIDTH_STANDARD;
      break;
  }
  
  // Calculate scale factors using logical dimensions
  double width_scale = static_cast<double>(logical_width) / optimal_width;
  double height_scale = static_cast<double>(logical_height) / BASE_WINDOW_HEIGHT;
  
  // Use the smaller scale to ensure everything fits
  return std::min(width_scale, height_scale);
}

void PyramidGame::autoFinishGame() {
  // We need to use a timer to handle the animations properly
  if (auto_finish_active_) {
    return; // Don't restart if already running
  }

  // Explicitly deactivate keyboard navigation and selection
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  // selected_pile_ = -1;
  // selected_card_idx_ = -1;

  auto_finish_active_ = true;

  // Try to make the first move immediately
  processNextAutoFinishMove();
}

void PyramidGame::processNextAutoFinishMove() {
  if (!auto_finish_active_) {
    return;
  }

  // If a foundation animation is currently running, wait for it to complete
  if (foundation_move_animation_active_) {
    // Set up a timer to check again after a short delay
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
    }
    auto_finish_timer_id_ = g_timeout_add(50, onAutoFinishTick, this);
    return;
  }

  bool found_move = false;

  // Check waste pile first
  if (!waste_.empty()) {
    const cardlib::Card &waste_card = waste_.back();

    // Try to move the waste card to foundation
    for (size_t f = 0; f < foundation_.size(); f++) {
      if (canMoveToFoundation(waste_card, f)) {
        // Play sound when a move is found and about to be executed
        playSound(GameSoundEvent::CardPlace);
        
        // Use the animation to move the card
        startFoundationMoveAnimation(waste_card, 1, 0, f + 2);

        // Remove card from waste pile
        waste_.pop_back();

        found_move = true;
        break;
      }
    }
  }

  // Try each tableau pile if no move was found yet
  if (!found_move) {
    for (size_t t = 0; t < tableau_.size(); t++) {
      auto &pile = tableau_[t];

      if (!pile.empty() && pile.back().face_up) {
        const cardlib::Card &top_card = pile.back().card;

        // Try to move to foundation
        for (size_t f = 0; f < foundation_.size(); f++) {
          if (canMoveToFoundation(top_card, f)) {
            // Play sound when a move is found and about to be executed
            playSound(GameSoundEvent::CardPlace);
            
            // Use the animation to move the card
            startFoundationMoveAnimation(top_card, t + 6, pile.size() - 1, f + 2);

            // Remove card from tableau
            pile.pop_back();

            // Flip the new top card if needed
            if (!pile.empty() && !pile.back().face_up) {
              playSound(GameSoundEvent::CardFlip);
              pile.back().face_up = true;
            }

            found_move = true;
            break;
          }
        }

        if (found_move) {
          break;
        }
      }
    }
  }

  if (found_move) {
    // Set up a timer to check for the next move after the animation completes
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
    }
    auto_finish_timer_id_ = g_timeout_add(200, onAutoFinishTick, this);
  } else {
    // No more moves to make
    auto_finish_active_ = false;
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
      auto_finish_timer_id_ = 0;
    }

    // Check if the player has won
    if (checkWinCondition()) {
      // FIX: Use conditional to call the right animation function for the active renderer
        startWinAnimation();
    }
  }
}

gboolean PyramidGame::onAutoFinishTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->processNextAutoFinishMove();
  return FALSE; // Don't repeat the timer
}

void PyramidGame::promptForSeed() {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Enter Seed", GTK_WINDOW(window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      "_Cancel", GTK_RESPONSE_CANCEL,
      "_OK", GTK_RESPONSE_ACCEPT,
      NULL);

  // Set the default response to ACCEPT (OK button)
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

  GtkWidget *label = gtk_label_new("Enter a number to use as the game seed:");
  gtk_container_add(GTK_CONTAINER(content_area), label);

  // Create an entry with the current seed as the default value
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entry), std::to_string(current_seed_).c_str());
  
  // Select all text by default so it's easy to replace
  gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
  
  // Make the entry activate the default response (OK button) when Enter is pressed
  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
  
  gtk_container_add(GTK_CONTAINER(content_area), entry);

  gtk_widget_show_all(dialog);

  // Create tooltip for the seed entry field to provide more context
  gtk_widget_set_tooltip_text(entry, "Current game seed. Press Enter to accept.");

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    try {
      current_seed_ = std::stoul(text);
      initializeGame();
      refreshDisplay();
      updateWindowTitle();
    } catch (...) {
      // Invalid input, show an error message
      GtkWidget *error_dialog = gtk_message_dialog_new(
          GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
          "Invalid seed. Please enter a valid number.");
      gtk_dialog_run(GTK_DIALOG(error_dialog));
      gtk_widget_destroy(error_dialog);
    }
  }

  gtk_widget_destroy(dialog);
}

void PyramidGame::showHowToPlay() {
  // Create dialog with OK button
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "How To Play Pyramid Solitaire", GTK_WINDOW(window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      "OK", GTK_RESPONSE_OK, NULL);

  // Set minimum dialog size
  gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

  // Create and configure the content area
  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 24);
  gtk_widget_set_margin_bottom(content_area, 12);

  // Create a label with the title
  GtkWidget *title_label = gtk_label_new(NULL);
  const char *title_markup = "<span size='x-large' weight='bold'>How To Play Pyramid Solitaire</span>";
  gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
  gtk_container_add(GTK_CONTAINER(content_area), title_label);
  gtk_widget_set_margin_bottom(title_label, 12);

  // Create a text view for the instructions
  GtkWidget *instructions_text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(instructions_text), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(instructions_text), 12);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(instructions_text), 12);

  // Add instructions text
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(instructions_text));
  const char *instructions =
      "Objective:\n"
      "Build four ordered card piles at the top of the screen, one for each suit (♣,♦,♥,♠), "
      "starting with Aces and ending with Kings.\n\n"
      "Game Setup:\n"
      "- Seven columns of cards are dealt from left to right\n"
      "- Each column contains one more card than the column to its left\n"
      "- The top card of each column is face up\n"
      "- Remaining cards form the draw pile in the upper left\n\n"
      "Rules:\n"
      "1. In the tableau (main playing area), stack cards in descending order (King to Ace) "
      "with alternating colors (red on black or black on red)\n"
      "2. Move single cards or stacks of cards between columns\n"
      "3. When you move a card that was covering a face-down card, the face-down card is "
      "flipped over\n"
      "4. Click the draw pile to reveal new cards when you need them\n"
      "5. Build the four foundation piles at the top in ascending order (A,2,3...K) of the same suit\n"
      "6. Empty spaces in the tableau can only be filled with Kings\n"
      "7. The game is won when all cards are moved to the foundation piles\n\n"
      "Controls:\n"
      "- Left-click and drag to move cards\n"
      "- Right-click to automatically move cards to the foundation piles\n"
      "- Use the keyboard for navigation (see Keyboard Shortcuts in the Help menu)\n"
      "- Use the Auto Finish feature (press F) when you're confident the game can be completed\n\n"
      "Tips:\n"
      "- Try to uncover face-down cards as soon as possible\n"
      "- Keep color alternation in mind when planning moves\n"
      "- Create empty columns to give yourself more flexibility\n"
      "- Move cards to the foundations only when it won't block other important moves";

  gtk_text_buffer_set_text(buffer, instructions, -1);

  // Add text view to a scrolled window
  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(scrolled_window, 550, 400);
  gtk_container_add(GTK_CONTAINER(scrolled_window), instructions_text);
  gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

  // Show all widgets and run the dialog
  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showKeyboardShortcuts() {
  // Create dialog with OK button
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Keyboard Shortcuts", GTK_WINDOW(window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      "OK", GTK_RESPONSE_OK, NULL);

  // Set dialog size
  gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 450);

  // Create and configure the content area
  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 24);
  gtk_widget_set_margin_bottom(content_area, 12);

  // Create a label with the title
  GtkWidget *title_label = gtk_label_new(NULL);
  const char *title_markup = "<span size='x-large' weight='bold'>Keyboard Shortcuts</span>";
  gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
  gtk_container_add(GTK_CONTAINER(content_area), title_label);
  gtk_widget_set_margin_bottom(title_label, 12);

  // Create a grid to organize shortcuts
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
  gtk_container_add(GTK_CONTAINER(content_area), grid);

  // Add header labels
  GtkWidget *key_header = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(key_header), "<b>Key</b>");
  gtk_grid_attach(GTK_GRID(grid), key_header, 0, 0, 1, 1);

  GtkWidget *action_header = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(action_header), "<b>Action</b>");
  gtk_grid_attach(GTK_GRID(grid), action_header, 1, 0, 1, 1);

  // Define shortcuts
  struct {
    const char *key;
    const char *action;
  } shortcuts[] = {
      {"Arrow Keys (←, →, ↑, ↓)", "Navigate between piles and cards"},
      {"Enter", "Select a card or perform a move"},
      {"Escape", "Cancel a selection or exit fullscreen"},
      {"Space", "Draw cards from the stock pile"},
      {"F", "Auto-finish (automatically move all possible cards to foundation)"},
      {"1", "Switch to Draw One mode"},
      {"3", "Switch to Draw Three mode"},
      {"F11", "Toggle fullscreen mode"},
      {"Ctrl+N", "New game"},
      {"Ctrl+L", "Load custom deck"},
      {"Ctrl+S", "Toggle sound on/off"},
      {"Ctrl+H", "Show About dialog"},
      {"Ctrl+Q", "Quit the game"},
      {"F1", "Show How To Play / About dialog"}
  };

  // Add shortcut rows
  for (int i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++) {
    GtkWidget *key_label = gtk_label_new(shortcuts[i].key);
    gtk_widget_set_halign(key_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), key_label, 0, i + 1, 1, 1);

    GtkWidget *action_label = gtk_label_new(shortcuts[i].action);
    gtk_widget_set_halign(action_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), action_label, 1, i + 1, 1, 1);
  }

  // Show all widgets and run the dialog
  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showDirectoryStructureDialog(const std::string &directory) {
  // Create dialog with OK button
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Directory Contents - Debugging Info",
      GTK_WINDOW(window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      "OK", GTK_RESPONSE_OK, NULL);

  // Set dialog size
  gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

  // Create and configure the content area
  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 12);

  // Create a text view for the directory contents
  GtkWidget *text_view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 8);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 8);

  // Use monospace font for better readability
  PangoFontDescription *font_desc = pango_font_description_from_string("Monospace 10");
  gtk_widget_override_font(text_view, font_desc);
  pango_font_description_free(font_desc);

  // Get directory structure as string
  std::string dir_info = getDirectoryStructure(directory);

  // Set the text
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
  gtk_text_buffer_set_text(buffer, dir_info.c_str(), -1);

  // Add text view to a scrolled window
  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(scrolled_window, 550, 350);
  gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);
  gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

  // Show all widgets and run the dialog
  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showMissingFileDialog(const std::string &filename, 
                                          const std::string &details) {
  // Create a dialog to show missing file error
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_OK,
      "Missing Required File");
  
  // Set detailed message
  std::string message = "Could not find " + filename + ".\n\n" + details + 
                        "\n\nPlease ensure the file is in the application directory.";
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), 
                                           "%s", message.c_str());
  
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showErrorDialog(const std::string &title, 
                                    const std::string &message) {
  // Create a dialog to show error
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_OK,
      "%s", title.c_str());
  
  // Set detailed message
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), 
                                           "%s", message.c_str());
  
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}
