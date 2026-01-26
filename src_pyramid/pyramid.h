#ifndef PYRAMID_SOLITAIRE_H
#define PYRAMID_SOLITAIRE_H

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
// STRUCTURES
// ============================================================================

struct CardFragment {
  double x, y, width, height;
  double velocity_x, velocity_y;
  double rotation, rotation_velocity;
  double target_x, target_y;
  cairo_surface_t *surface;
  bool active;
};

struct AnimatedCard {
  cardlib::Card card;
  double x, y;
  double velocity_x, velocity_y;
  double rotation, rotation_velocity;
  bool active, exploded;
  double target_x, target_y;
  bool face_up;
  std::vector<CardFragment> fragments;
};

struct PyramidCard {
  cardlib::Card card;
  bool face_up;
  bool removed;
  
  PyramidCard(const cardlib::Card &c = cardlib::Card(), bool up = true, bool rem = false)
    : card(c), face_up(up), removed(rem) {}
};

// ============================================================================
// MAIN CLASS
// ============================================================================

class PyramidGame {
public:
  enum class GameMode {
    PYRAMID_SINGLE = 0,
    PYRAMID_DOUBLE = 1
  };

  enum class GameSoundEvent {
    CardFlip,
    CardPlace,
    CardRemove,
    WinGame,
    DealCard,
    Firework,
    NoMatch
  };

  enum class RenderingEngine {
    CAIRO,
    OPENGL
  };

  PyramidGame();
  ~PyramidGame();

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
  // CONSTANTS
  // ========================================================================
  static constexpr int BASE_WINDOW_WIDTH = 1024;
  static constexpr int BASE_WINDOW_HEIGHT = 768;
  static constexpr int BASE_CARD_WIDTH = 100;
  static constexpr int BASE_CARD_HEIGHT = 145;
  static constexpr int BASE_CARD_SPACING = 20;
  static constexpr int BASE_VERT_SPACING = 30;

  static constexpr double GRAVITY = 0.8;
  static constexpr double BOUNCE_FACTOR = -0.7;
  static constexpr int ANIMATION_INTERVAL = 16;
  static constexpr double CARD_REMOVE_SPEED = 0.4;
  static constexpr double DEAL_INTERVAL = 30;
  static constexpr double DEAL_SPEED = 1.3;
  static constexpr double EXPLOSION_THRESHOLD_MIN = 0.3;
  static constexpr double EXPLOSION_THRESHOLD_MAX = 0.7;

  // ========================================================================
  // GAME STATE
  // ========================================================================
  cardlib::Deck deck_;
  cardlib::MultiDeck multi_deck_;
  GameMode current_game_mode_ = GameMode::PYRAMID_SINGLE;
  unsigned int current_seed_;

  // Pyramid: 7 rows, each row i has (i+1) cards
  std::vector<std::vector<PyramidCard>> pyramid_;
  std::vector<cardlib::Card> stock_;
  std::vector<cardlib::Card> waste_;

  // Selection state
  int selected_card_row_ = -1;
  int selected_card_col_ = -1;
  bool card_selected_ = false;

  // Animations
  bool win_animation_active_ = false;
  std::vector<AnimatedCard> animated_cards_;
  int cards_launched_ = 0;
  double launch_timer_ = 0;
  guint animation_timer_id_ = 0;

  bool deal_animation_active_ = false;
  std::vector<AnimatedCard> deal_cards_;
  int cards_dealt_ = 0;
  double deal_timer_ = 0;

  bool auto_finish_active_ = false;
  guint auto_finish_timer_id_ = 0;

  // Rendering
  RenderingEngine rendering_engine_ = RenderingEngine::CAIRO;
  bool opengl_initialized_ = false;
  bool cairo_initialized_ = false;
  bool engine_switch_requested_ = false;
  RenderingEngine requested_engine_ = RenderingEngine::CAIRO;

  GdkGLContext *gl_context_ = nullptr;

  GtkWidget *window_ = nullptr;
  GtkWidget *vbox_ = nullptr;
  GtkWidget *game_area_ = nullptr;
  GtkWidget *gl_area_ = nullptr;
  cairo_surface_t *buffer_surface_ = nullptr;
  cairo_t *buffer_cr_ = nullptr;
  GtkWidget *rendering_stack_ = nullptr;

  int game_area_width_ = BASE_WINDOW_WIDTH;
  int game_area_height_ = BASE_WINDOW_HEIGHT;

  bool is_fullscreen_ = false;
  bool draw_three_mode_ = true;

  // Card sizing
  int current_card_width_ = BASE_CARD_WIDTH;
  int current_card_height_ = BASE_CARD_HEIGHT;
  int current_card_spacing_ = BASE_CARD_SPACING;
  int current_vert_spacing_ = BASE_VERT_SPACING;

  // Keyboard navigation
  int selected_pile_ = -1;
  int selected_card_idx_ = -1;
  bool keyboard_navigation_active_ = false;
  bool keyboard_selection_active_ = false;

  // Resources
  std::unordered_map<std::string, cairo_surface_t *> card_cache_;
  cairo_surface_t *card_back_surface_ = nullptr;
  cairo_surface_t *placeholder_surface_ = nullptr;

  bool sound_enabled_ = true;
  std::string sounds_zip_path_ = "sounds.zip";

  bool cache_dirty_ = false;

  // ========================================================================
  // GAME FLOW
  // ========================================================================
  void initializeGame();
  void dealInitialPyramid();
  void restartGame();
  void promptForSeed();

  // ========================================================================
  // DRAWING - CAIRO
  // ========================================================================
  static gboolean onDraw(GtkWidget *widget, cairo_t *cr, gpointer data);
  void drawGame(cairo_t *cr, int width, int height);
  void drawPyramid(cairo_t *cr, int width, int height);
  void drawStockAndWaste(cairo_t *cr, int width, int height);
  void drawCard(cairo_t *cr, const cardlib::Card &card, int x, int y, bool face_up);
  void drawCardBack(cairo_t *cr, int x, int y);
  void drawPyramidCard(cairo_t *cr, const PyramidCard &card, int x, int y, bool is_selected = false);

  // ========================================================================
  // DRAWING - OPENGL
  // ========================================================================
#ifdef USEOPENGL
  void drawGame_gl();
  void drawPyramid_gl(GLuint shaderProgram, GLuint VAO);
  void drawStockAndWaste_gl(GLuint shaderProgram, GLuint VAO);
  void highlightSelectedCard_gl(GLuint shaderProgram, GLuint VAO);
#endif

  // ========================================================================
  // ANIMATIONS
  // ========================================================================
  void startWinAnimation();
  void updateWinAnimation();
  void stopWinAnimation();
  void launchNextCard();
  void explodeCard(AnimatedCard &card);
  void updateCardFragments(AnimatedCard &card);
  static gboolean onWinAnimationTick(gpointer data);

#ifdef USEOPENGL
  void startWinAnimation_gl();
  void updateWinAnimation_gl();
  void stopWinAnimation_gl();
  void launchNextCard_gl();
  void explodeCard_gl(AnimatedCard &card);
  void updateCardFragments_gl(AnimatedCard &card);
  void drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO);
#endif

  void startDealAnimation();
  void updateDealAnimation();
  void dealNextCard();
  void completeDeal();
  void stopDealAnimation();
  static gboolean onDealAnimationTick(gpointer data);

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
  // INPUT HANDLING
  // ========================================================================
  static gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onMotionNotify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
  static gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data);

  // ========================================================================
  // MENU HANDLERS
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
  // GAME LOGIC
  // ========================================================================
  void selectPyramidCard(int row, int col);
  void selectStockCard();
  void selectWasteCard();
  bool isCardExposed(int row, int col) const;
  bool canRemovePair(const cardlib::Card &card1, const cardlib::Card &card2) const;
  bool canRemoveKing(const cardlib::Card &card) const;
  int getCardValue(const cardlib::Card &card) const;
  void removePair(int row1, int col1, int row2, int col2);
  void removeKing(int row, int col);
  void handleStockPileClick();
  bool checkWinCondition() const;
  int countRemovedCards() const;

  // ========================================================================
  // KEYBOARD NAVIGATION
  // ========================================================================
  void selectNextCard();
  void selectPreviousCard();
  void activateSelected();
  void resetKeyboardNavigation();

  // ========================================================================
  // UTILITIES
  // ========================================================================
  std::pair<int, int> getPileAt(int x, int y) const;
  void refreshDisplay();
  void toggleFullscreen();
  void updateCardDimensions(int window_width, int window_height);
  double getScaleFactor(int window_width, int window_height) const;

  // ========================================================================
  // UI HELPERS
  // ========================================================================
  void showHowToPlay();
  void showKeyboardShortcuts();
  void showGameStats();
  void showErrorDialog(const std::string &title, const std::string &message);

  // ========================================================================
  // RESOURCE MANAGEMENT
  // ========================================================================
  void cleanupCardCache();
  void cleanupAudio();
  bool loadDeck(const std::string &path);
  void cleanupResources();
  void clearAndRebuildCaches();

  // ========================================================================
  // SETTINGS
  // ========================================================================
  void initializeSettingsDir();
  void saveSettings();
  void loadSettings();

  // ========================================================================
  // SOUND
  // ========================================================================
  void checkAndInitializeSound();
  bool initializeAudio();
  bool loadSoundFromZip(GameSoundEvent event, const std::string &soundFileName);
  void playSound(GameSoundEvent event);
  bool extractFileFromZip(const std::string &zipFilePath, const std::string &fileName,
                          std::vector<uint8_t> &fileData);

  // ========================================================================
  // OPENGL
  // ========================================================================
#ifdef USEOPENGL
  GLuint setupShaders_gl();
  GLuint setupCardQuadVAO_gl();
  bool initializeCardTextures_gl();
  bool loadCardTexture_gl(const std::string &cardKey, const cardlib::Card &card);
  bool initializeOpenGLResources();
  void cleanupOpenGLResources_gl();
  bool validateOpenGLContext();
  bool reloadCustomCardBackTexture_gl();
  GLuint loadTextureFromMemory(const std::vector<unsigned char> &data);
  void renderFrame_gl();

  GLuint cardShaderProgram_gl_ = 0;
  GLuint simpleShaderProgram_gl_ = 0;
  GLuint cardQuadVAO_gl_ = 0;
  GLuint cardQuadVBO_gl_ = 0;
  GLuint cardQuadEBO_gl_ = 0;

  std::unordered_map<std::string, GLuint> cardTextures_gl_;
  GLuint cardBackTexture_gl_ = 0;
#endif

  void dealTestLayout();
};

#endif // PYRAMID_SOLITAIRE_H
