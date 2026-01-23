#ifndef FREECELL_H
#define FREECELL_H

// ============================================================================
// INCLUDES - System and External Libraries
// ============================================================================
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <gtk/gtk.h>
#include "cardlib.h"

#ifdef USEOPENGL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

// ============================================================================
// ENUMS
// ============================================================================

// Reusing GameSoundEvent from existing code
enum class GameSoundEvent {
  CardFlip,
  CardPlace,
  StockRefill,
  WinGame,
  DealCard,
  Firework,
  CardDeal
};

enum class GameMode {
  CLASSIC_FREECELL,
  DOUBLE_FREECELL
};

// ============================================================================
// STRUCTURES
// ============================================================================

// Reusing CardFragment struct
struct CardFragment {
  double x;
  double y;
  double width;
  double height;
  double velocity_x;
  double velocity_y;
  double rotation;
  double rotation_velocity;
  double target_x;  // Texture coordinate X for explosion fragments (grid column)
  double target_y;  // Texture coordinate Y for explosion fragments (grid row)
  cairo_surface_t *surface;
  bool active;
};

// Reusing AnimatedCard struct
struct AnimatedCard {
  cardlib::Card card;
  double x;
  double y;
  double velocity_x;
  double velocity_y;
  double rotation;
  double rotation_velocity;
  bool active;
  bool exploded;
  bool face_up;
  std::vector<CardFragment> fragments;
  int source_pile;

  // For deal animation
  double target_x;
  double target_y;
};

// ============================================================================
// FREE FUNCTION DECLARATIONS
// ============================================================================

#ifdef USEOPENGL
GLuint compileShader_gl(const char *source, GLenum shaderType);
GLuint createShaderProgram_gl(const char *vertexSrc, const char *fragmentSrc);
GLuint loadTextureFromMemory(const std::vector<unsigned char> &data);
#endif

// ============================================================================
// CLASS DEFINITION
// ============================================================================

class FreecellGame {
public:
  // ========================================================================
  // RENDERING ENGINE SELECTION
  // ========================================================================
  enum class RenderingEngine {
    CAIRO,    // CPU-based 2D rendering (original)
    OPENGL    // GPU-accelerated 3D rendering
  };

  FreecellGame();
  ~FreecellGame();

  // Engine control methods
  bool setRenderingEngine(RenderingEngine engine);
  RenderingEngine getRenderingEngine() const { return rendering_engine_; }
  bool isOpenGLSupported() const;
  bool initializeRenderingEngine();
  bool switchRenderingEngine(RenderingEngine newEngine);
  void cleanupRenderingEngine();
  std::string getRenderingEngineName() const;
  void printEngineInfo();
  void addEngineSelectionMenu(GtkWidget *menubar);
  void saveEnginePreference();
  void loadEnginePreference();
  void renderFrame();

  bool setSoundsZipPath(const std::string &path);
  void run(int argc, char **argv);

private:
  // ========================================================================
  // GAME STATE - CONSTANTS
  // ========================================================================
  static constexpr int BASE_WINDOW_WIDTH = 1024;
  static constexpr int BASE_WINDOW_HEIGHT = 768;
  static constexpr int BASE_CARD_WIDTH = 100;
  static constexpr int BASE_CARD_HEIGHT = 145;
  static constexpr int BASE_CARD_SPACING = 20;
  static constexpr int BASE_VERT_SPACING = 25;

  static constexpr double GRAVITY = 0.8;
  static constexpr double BOUNCE_FACTOR = -0.7;
  static constexpr int ANIMATION_INTERVAL = 16; // ~60 FPS

  static constexpr double FOUNDATION_MOVE_SPEED = 0.4;
  static constexpr double DEAL_INTERVAL = 30;   // Time between dealing cards (ms)
  static constexpr double DEAL_SPEED = 1.3;     // Speed multiplier for dealing

  // ========================================================================
  // GAME STATE - CORE GAME DATA
  // ========================================================================
  cardlib::Deck deck_;
  cardlib::MultiDeck multi_deck_ = cardlib::MultiDeck(1);
  bool has_multi_deck_ = false;

  std::vector<std::optional<cardlib::Card>> freecells_;      // 4 Free cells for temporary storage
  std::vector<std::vector<cardlib::Card>> foundation_;        // 4 piles for aces (one per suit)
  std::vector<std::vector<cardlib::Card>> tableau_;           // 8 tableau columns
  std::vector<std::vector<cardlib::Card>> freecell_animation_cards_;

  GameMode current_game_mode_ = GameMode::CLASSIC_FREECELL;
  unsigned int current_seed_;

  // ========================================================================
  // GAME STATE - DRAG AND DROP
  // ========================================================================
  bool dragging_ = false;
  std::optional<cardlib::Card> drag_card_;
  int drag_source_pile_;
  int drag_source_card_idx_;
  int drag_start_x_, drag_start_y_;
  double drag_offset_x_;
  double drag_offset_y_;
  std::vector<cardlib::Card> drag_cards_;

  // ========================================================================
  // GAME STATE - ANIMATIONS
  // ========================================================================

  // Win animation fields
  bool win_animation_active_ = false;
  std::vector<AnimatedCard> animated_cards_;
  int cards_launched_ = 0;
  double launch_timer_ = 0;
  guint animation_timer_id_ = 0;

  // Deal animation fields
  bool deal_animation_active_ = false;
  std::vector<AnimatedCard> deal_cards_;
  int cards_dealt_ = 0;
  double deal_timer_ = 0;

  // Foundation move animation fields
  bool foundation_move_animation_active_ = false;
  AnimatedCard foundation_move_card_;
  int foundation_source_pile_ = -1;
  int foundation_target_pile_ = -1;
  double foundation_move_timer_ = 0;

  // Auto-finish animation fields
  bool auto_finish_active_ = false;
  guint auto_finish_timer_id_ = 0;
  std::vector<std::vector<bool>> animated_foundation_cards_;
  std::vector<bool> animated_freecell_cards_;

  // ========================================================================
  // GAME STATE - RENDERING ENGINE
  // ========================================================================
  RenderingEngine rendering_engine_;
  bool opengl_initialized_ = false;
  bool cairo_initialized_ = false;
  bool engine_switch_requested_ = false;
  RenderingEngine requested_engine_;
  bool is_glew_initialized_ = false;

  // ========================================================================
  // GAME STATE - UI DIMENSIONS
  // ========================================================================
  int current_card_width_;
  int current_card_height_;
  int current_card_spacing_;
  int current_vert_spacing_;
  GtkAllocation allocation;

  // ========================================================================
  // GAME STATE - KEYBOARD NAVIGATION
  // ========================================================================
  int selected_pile_ = -1;             // Currently selected pile (-1 if none)
  int selected_card_idx_;              // Index of selected card in the pile
  bool keyboard_navigation_active_ = false;
  bool keyboard_selection_active_ = false;
  int source_pile_;
  int source_card_idx_;

  // ========================================================================
  // GAME STATE - SETTINGS AND PREFERENCES
  // ========================================================================
  std::string settings_dir_;
  std::string sounds_zip_path_;
  std::string custom_back_path_;
  bool sound_enabled_ = false;
  bool is_fullscreen_ = false;
  bool cache_dirty_ = false;            // Flag to indicate caches need to be cleared and rebuilt
  bool game_fully_initialized_ = false; // Track if game is fully initialized

  // ========================================================================
  // GAME STATE - CACHING AND BUFFERS
  // ========================================================================
  std::unordered_map<std::string, cairo_surface_t *> card_surface_cache_;
  cairo_surface_t *buffer_surface_ = nullptr;
  cairo_t *buffer_cr_ = nullptr;

  // ========================================================================
  // GTK WIDGETS
  // ========================================================================
  GtkWidget *window_;
  GtkWidget *game_area_;        // Cairo rendering area
  GtkWidget *gl_area_;          // OpenGL rendering area
  GtkWidget *rendering_stack_;  // Stack to switch between them
  GtkWidget *vbox_;             // Vertical box to hold menu and game area

  // Menu radio items for game modes
  GtkWidget *classic_mode_item_ = nullptr;
  GtkWidget *double_mode_item_ = nullptr;

  // ========================================================================
  // INITIALIZATION AND SETUP METHODS
  // ========================================================================
  void initializeGame();
  void deal();
  void setupWindow();
  void setupGameArea();
  void setupCairoArea();
  void setupMenuBar();
  void initializeSettingsDir();
  void initializeCardCache();
  void clearAndRebuildCaches();

#ifdef USEOPENGL
  void setupOpenGLArea();
  bool initializeOpenGLResources();
  bool initializeGLEW();
  bool checkOpenGLCapabilities();
  void logOpenGLInfo();
  bool initializeRenderingEngine_gl();
#endif

  // ========================================================================
  // SETTINGS AND PREFERENCES METHODS
  // ========================================================================
  bool loadSettings();
  void saveSettings();
  void setGameMode(GameMode mode);
  void updateLayoutForGameMode();

  // ========================================================================
  // CARD DRAWING METHODS - CAIRO
  // ========================================================================
  void drawCard(cairo_t *cr, int x, int y, const cardlib::Card *card);
  void drawEmptyPile(cairo_t *cr, int x, int y);
  void drawAnimatedCard(cairo_t *cr, const AnimatedCard &anim_card);
  void drawCardFragment(cairo_t *cr, const CardFragment &fragment);
  void highlightSelectedCard(cairo_t *cr);

  cairo_surface_t *getCardSurface(const cardlib::Card &card);
  void initializeDrawBuffer(int width, int height);

  // ========================================================================
  // GAME PILE DRAWING METHODS - CAIRO
  // ========================================================================
  void drawFreecells();
  void drawFoundationPiles();
  void drawTableau();
  void drawTableauDuringDealAnimation(int column_index, int x, int tableau_y);
  void drawNormalTableauColumn(int column_index, int x, int tableau_y);
  void drawDraggedCards();
  void drawAnimations();
  void drawWinAnimation();

  // ========================================================================
  // CARD DRAWING METHODS - OPENGL
  // ========================================================================
#ifdef USEOPENGL
  void drawCard_gl(const cardlib::Card &card, int x, int y, bool face_up);
  void drawEmptyPile_gl(int x, int y);
  void drawAnimatedCard_gl(const AnimatedCard &anim_card, GLuint shaderProgram, GLuint VAO);
  void drawCardFragment_gl(const CardFragment &fragment, const AnimatedCard &card, GLuint shaderProgram, GLuint VAO);
  void highlightSelectedCard_gl();

  // Game pile drawing functions - OpenGL versions
  void drawFreecells_gl(GLuint shaderProgram, GLuint VAO);
  void drawFoundationPiles_gl(GLuint shaderProgram, GLuint VAO);
  void drawFoundationDuringWinAnimation_gl(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y);
  void drawNormalFoundationPile_gl(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y);
  void drawTableau_gl(GLuint shaderProgram, GLuint VAO);
  void drawTableauDuringDealAnimation_gl(int column_index, int x, int tableau_y);
  void drawNormalTableauColumn_gl(int column_index, int x, int tableau_y);
  void drawDraggedCards_gl(GLuint shaderProgram, GLuint VAO);

  void draw_comet_buster_gl(void *vis_ptr, void *other);
  void renderFrame_gl();
#endif

  // ========================================================================
  // ANIMATION METHODS - WIN ANIMATION (CAIRO)
  // ========================================================================
  void startWinAnimation();
  void updateWinAnimation();
  void stopWinAnimation();
  void launchNextCard();
  void explodeCard(AnimatedCard &card);
  void updateCardFragments(AnimatedCard &card);
  static gboolean onAnimationTick(gpointer data);

  // ========================================================================
  // ANIMATION METHODS - WIN ANIMATION (OPENGL)
  // ========================================================================
#ifdef USEOPENGL
  void startWinAnimation_gl();
  void updateWinAnimation_gl();
  void stopWinAnimation_gl();
  void launchNextCard_gl();
  void explodeCard_gl(AnimatedCard &card);
  void updateCardFragments_gl(AnimatedCard &card);
  void drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO);
#endif

  // ========================================================================
  // ANIMATION METHODS - DEAL ANIMATION (CAIRO)
  // ========================================================================
  void startDealAnimation();
  void updateDealAnimation();
  void dealNextCard();
  void completeDeal();
  static gboolean onDealAnimationTick(gpointer data);

  // ========================================================================
  // ANIMATION METHODS - DEAL ANIMATION (OPENGL)
  // ========================================================================
#ifdef USEOPENGL
  void startDealAnimation_gl();
  void updateDealAnimation_gl();
  void dealNextCard_gl();
  void completeDeal_gl();
  void stopDealAnimation_gl();
  void drawDealAnimation_gl(GLuint shaderProgram, GLuint VAO);
  static gboolean onDealAnimationTick_gl(gpointer data);
#endif

  // ========================================================================
  // ANIMATION METHODS - FOUNDATION MOVE ANIMATION (CAIRO)
  // ========================================================================
  void startFoundationMoveAnimation(const cardlib::Card &card, int source_pile, int source_index, int target_pile);
  void updateFoundationMoveAnimation();
  static gboolean onFoundationMoveAnimationTick(gpointer data);

  // ========================================================================
  // ANIMATION METHODS - FOUNDATION MOVE ANIMATION (OPENGL)
  // ========================================================================
#ifdef USEOPENGL
  void startFoundationMoveAnimation_gl(const cardlib::Card &card, int source_pile, int source_index, int target_pile);
  void updateFoundationMoveAnimation_gl();
  void drawFoundationAnimation_gl(GLuint shaderProgram, GLuint VAO);
  static gboolean onFoundationMoveAnimationTick_gl(gpointer data);
#endif

  // ========================================================================
  // ANIMATION METHODS - AUTO FINISH
  // ========================================================================
  void autoFinishGame();
  void processNextAutoFinishMove();
  static gboolean onAutoFinishTick(gpointer data);

#ifdef USEOPENGL
  void processNextAutoFinishMove_gl();
  static gboolean onAutoFinishTick_gl(gpointer data);
#endif

  // ========================================================================
  // EVENT HANDLERS - INPUT
  // ========================================================================
  static gboolean onDraw(GtkWidget *widget, cairo_t *cr, gpointer data);
  static gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onMotionNotify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
  static gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data);

  // ========================================================================
  // EVENT HANDLERS - MENU
  // ========================================================================
  static void onNewGame(GtkWidget *widget, gpointer data);
  static void onQuit(GtkWidget *widget, gpointer data);
  static void onAbout(GtkWidget *widget, gpointer data);
  static void onToggleFullscreen(GtkWidget *widget, gpointer data);

#ifdef USEOPENGL
  static gboolean onGLRealize(GtkGLArea *area, gpointer data);
  static gboolean onGLRender(GtkGLArea *area, GdkGLContext *context, gpointer data);
#endif

  // ========================================================================
  // CARD MOVEMENT AND VALIDATION METHODS
  // ========================================================================
  bool tryMoveFromFreecell();
  bool tryMoveFromFoundation();
  bool tryMoveFromTableau();
  bool canMoveToFoundation(const cardlib::Card &card, int foundation_idx) const;
  bool canMoveToTableau(const cardlib::Card &card, int tableau_idx) const;
  bool canMoveTableauStack(const std::vector<cardlib::Card> &cards, int tableau_idx) const;
  bool isValidTableauSequence(const std::vector<cardlib::Card> &cards) const;
  bool isCardRed(const cardlib::Card &card) const;
  bool isValidDragSource(int pile_index, int card_index) const;
  bool checkWinCondition() const;
  int findFirstPlayableCard(int tableau_idx);
  bool autoFinishMoves();

  // ========================================================================
  // KEYBOARD NAVIGATION METHODS
  // ========================================================================
  void selectNextPile();
  void selectPreviousPile();
  void selectCardUp();
  void selectCardDown();
  void activateSelected();
  void resetKeyboardNavigation();
  bool tryMoveSelectedCard();
  bool canSelectForMove();
  bool isCardPlayable();
  bool handleSpacebarAction();

  // ========================================================================
  // UTILITY METHODS
  // ========================================================================
  std::pair<int, int> getPileAt(int x, int y) const;
  void refreshDisplay();
  void updateCardDimensions(int window_width, int window_height);
  double getScaleFactor(int window_width, int window_height) const;
  void toggleFullscreen();
  void setupEasyGame();
  void promptForSeed();
  void restartGame();
  void launchCardFromFreecell();

  // ========================================================================
  // CACHE AND RESOURCE CLEANUP METHODS
  // ========================================================================
  void cleanupCardCache();
  void cleanupAudio();

  // ========================================================================
  // SOUND SYSTEM METHODS
  // ========================================================================
  bool initializeAudio();
  bool loadSoundFromZip(GameSoundEvent event, const std::string &soundFileName);
  void playSound(GameSoundEvent event);
  bool extractFileFromZip(const std::string &zipFilePath, const std::string &fileName, std::vector<uint8_t> &fileData);

  // ========================================================================
  // OPENGL RESOURCE MANAGEMENT
  // ========================================================================
#ifdef USEOPENGL
  GLuint setupShaders_gl();
  GLuint setupCardQuadVAO_gl();
  bool initializeCardTextures_gl();
  void cleanupOpenGLResources_gl();
  bool validateOpenGLContext();
  bool reloadCustomCardBackTexture_gl();
  void drawStockToWasteAnimation_gl(GLuint shaderProgram, GLuint VAO);

  // OpenGL 3.4 Rendering Components
  GLuint cardShaderProgram_gl_ = 0;      // Main card rendering shader
  GLuint simpleShaderProgram_gl_ = 0;    // Simple color rendering shader
  GLuint cardQuadVAO_gl_ = 0;            // Vertex Array Object for card quad
  GLuint cardQuadVBO_gl_ = 0;            // Vertex Buffer Object
  GLuint cardQuadEBO_gl_ = 0;            // Element Buffer Object

  std::unordered_map<std::string, GLuint> cardTextures_gl_;  // Texture cache
  GLuint cardBackTexture_gl_ = 0;                             // Card back texture
#endif
};

#endif // FREECELL_H
