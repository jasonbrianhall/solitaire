#ifndef SOLITAIRE_H
#define SOLITAIRE_H

#include "cardlib.h"
#include <gtk/gtk.h>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

class SolitaireGame {
public:
  SolitaireGame();
  ~SolitaireGame();
  bool setSoundsZipPath(const std::string &path);

  void run(int argc, char **argv);

  enum class GameMode {
    STANDARD_KLONDIKE,  // Single deck
    DOUBLE_KLONDIKE,    // Two decks
    TRIPLE_KLONDIKE     // Three decks (for future expansion)
  };

enum class GameSoundEvent {
  CardFlip,
  CardPlace,
  StockRefill,
  WinGame,
  DealCard,
  Firework
};

struct CardFragment {
  double x;
  double y;
  double width;
  double height;
  double velocity_x;
  double velocity_y;
  double rotation;
  double rotation_velocity;
  cairo_surface_t *surface;
  bool active;
};

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
  std::vector<CardFragment> fragments;

  // New fields for deal animation
  double target_x;
  double target_y;
  bool face_up;
};

struct TableauCard {
  cardlib::Card card;
  bool face_up;

  TableauCard(const cardlib::Card &c, bool up) : card(c), face_up(up) {}
};

private:
  // Game state
  static constexpr int BASE_WINDOW_WIDTH = 1024;
  static constexpr int BASE_WINDOW_HEIGHT = 768;
  static constexpr int BASE_CARD_WIDTH = 100;
  static constexpr int BASE_CARD_HEIGHT = 145;
  static constexpr int BASE_CARD_SPACING = 20;
  static constexpr int BASE_VERT_SPACING = 30;
  std::vector<std::vector<bool>> animated_foundation_cards_;

  bool foundation_move_animation_active_ = false;
  AnimatedCard foundation_move_card_;
  int foundation_target_pile_ = -1;
  double foundation_move_timer_ = 0;
  static constexpr double FOUNDATION_MOVE_SPEED = 0.4;

  // Current dynamic dimensions
  int current_card_width_;
  int current_card_height_;
  int current_card_spacing_;
  int current_vert_spacing_;

  // New method declarations
  void updateCardDimensions(int window_width, int window_height);
  double getScaleFactor(int window_width, int window_height) const;

  // Win animation fields
  bool win_animation_active_ = false;
  std::vector<AnimatedCard> animated_cards_;
  guint animation_timer_id_ = 0;
  static constexpr double GRAVITY = 0.8;
  static constexpr double BOUNCE_FACTOR = -0.7;
  static constexpr int ANIMATION_INTERVAL = 16; // ~60 FPS
  int cards_launched_ = 0;
  double launch_timer_ = 0;

  // Deal animation fields
  bool deal_animation_active_ = false;
  std::vector<AnimatedCard> deal_cards_;
  int cards_dealt_ = 0;
  double deal_timer_ = 0;
  static constexpr double DEAL_INTERVAL = 30; // Time between dealing cards (ms)
  static constexpr double DEAL_SPEED = 1.3;   // Speed multiplier for dealing

  // Animation methods (Cairo versions - kept for compatibility)
  void startWinAnimation();
  void updateWinAnimation();
  static gboolean onAnimationTick(gpointer data);
  void stopWinAnimation();
  void launchNextCard();

  void startDealAnimation();
  void updateDealAnimation();
  static gboolean onDealAnimationTick(gpointer data);
  void stopDealAnimation();
  void dealNextCard();
  void completeDeal();

  // ============================================================================
  // OpenGL 3.4 Animation Methods - Complete Set
  // ============================================================================

  // OpenGL 3.4 Rendering Components
  GLuint cardShaderProgram_gl_       = 0;  // Main card rendering shader
  GLuint simpleShaderProgram_gl_     = 0;  // Simple color rendering shader
  GLuint cardQuadVAO_gl_             = 0;  // Vertex Array Object for card quad
  GLuint cardQuadVBO_gl_             = 0;  // Vertex Buffer Object
  GLuint cardQuadEBO_gl_             = 0;  // Element Buffer Object
  
  std::unordered_map<std::string, GLuint> cardTextures_gl_;  // Texture cache
  GLuint cardBackTexture_gl_         = 0;  // Card back texture

  // Win Animation - OpenGL 3.4 Versions
  void updateWinAnimation_gl();
  void startWinAnimation_gl();
  void stopWinAnimation_gl();
  static gboolean onAnimationTick_gl(gpointer data);
  void launchNextCard_gl();
  void explodeCard_gl(AnimatedCard &card);
  void updateCardFragments_gl(AnimatedCard &card);

  // Deal Animation - OpenGL 3.4 Versions
  void startDealAnimation_gl();
  void updateDealAnimation_gl();
  static gboolean onDealAnimationTick_gl(gpointer data);
  void dealNextCard_gl();
  void completeDeal_gl();
  void stopDealAnimation_gl();

  // Foundation Move Animation - OpenGL 3.4 Versions
  void startFoundationMoveAnimation_gl(const cardlib::Card &card,
                                       int source_pile,
                                       int source_index,
                                       int target_pile);
  void updateFoundationMoveAnimation_gl();
  static gboolean onFoundationMoveAnimationTick_gl(gpointer data);

  // Stock to Waste Animation - OpenGL 3.4 Versions
  void startStockToWasteAnimation_gl();
  void updateStockToWasteAnimation_gl();
  static gboolean onStockToWasteAnimationTick_gl(gpointer data);
  void completeStockToWasteAnimation_gl();

  // Drawing Functions - OpenGL 3.4 Versions
  void drawAnimatedCard_gl(const AnimatedCard &anim_card,
                           GLuint shaderProgram,
                           GLuint VAO);
  void drawCardFragment_gl(const CardFragment &fragment,
                           GLuint shaderProgram,
                           GLuint VAO);
  void drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO);
  void drawDealAnimation_gl(GLuint shaderProgram, GLuint VAO);
  void drawFoundationAnimation_gl(GLuint shaderProgram, GLuint VAO);
  void drawStockToWasteAnimation_gl(GLuint shaderProgram, GLuint VAO);

  // OpenGL Setup and Initialization Functions
  GLuint setupCardQuadVAO_gl();      // Create VAO/VBO for card quads
  GLuint setupShaders_gl();          // Compile and link shaders
  bool initializeCardTextures_gl();  // Load card images as OpenGL textures
  void cleanupOpenGLResources_gl();  // Clean up OpenGL resources
  bool loadCardTexture_gl(const std::string &cardKey,
                          const cardlib::Card &card);

  // Auto-Finish Animation - OpenGL 3.4 Version
  static gboolean onAutoFinishTick_gl(gpointer data);
  void processNextAutoFinishMove_gl();

  // ============================================================================
  // Original Cairo Animation Methods (for backward compatibility)
  // ============================================================================

  cardlib::Deck deck_;
  std::vector<cardlib::Card> stock_; // Draw pile
  std::vector<cardlib::Card> waste_; // Faced-up cards from stock
  std::vector<std::vector<cardlib::Card>> foundation_; // 4 piles for aces
  std::vector<std::vector<TableauCard>> tableau_;

  // Helper function to convert TableauCard vector to Card vector
  std::vector<cardlib::Card>
  getTableauCardsAsCards(const std::vector<TableauCard> &tableau_cards,
                         int start_index);

  // Method to get cards for dragging
  std::vector<cardlib::Card> getDragCards(int pile_index, int card_index);
  void handleStockPileClick();
  void drawCard(cairo_t *cr, int x, int y, const cardlib::Card *card,
                bool face_up);
  void flipTopTableauCard(int);
  // Drag and drop state
  bool dragging_;
  GtkWidget *drag_source_;
  std::vector<cardlib::Card> drag_cards_;
  int drag_source_pile_;
  int drag_start_x_, drag_start_y_;

  // GTK widgets
  GtkWidget *window_;
  GtkWidget *game_area_;
  std::vector<GtkWidget *> card_widgets_;

  // Card dimensions and spacing
  static constexpr int CARD_WIDTH = 100;
  static constexpr int CARD_HEIGHT = 145;
  static constexpr int CARD_SPACING = 20;
  static constexpr int VERT_SPACING = 30;

  // Initialize game
  void initializeGame();
  void deal();

  // UI setup
  void setupWindow();
  void setupGameArea();
  GtkWidget *createCardWidget(const cardlib::Card &card, bool face_up);

  // Game logic
  bool canMoveToPile(const std::vector<cardlib::Card> &cards,
                     const std::vector<cardlib::Card> &target,
                     bool is_foundation = false) const;
  bool canMoveToFoundation(const cardlib::Card &card,
                           int foundation_index) const;
  void moveCards(std::vector<cardlib::Card> &from,
                 std::vector<cardlib::Card> &to, size_t count);
  bool checkWinCondition() const;

  // Event handlers
  static gboolean onDraw(GtkWidget *widget, cairo_t *cr, gpointer data);
  static gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                gpointer data);
  static gboolean onButtonRelease(GtkWidget *widget, GdkEventButton *event,
                                  gpointer data);
  static gboolean onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer data);

  // Helper functions
  std::pair<int, int> getPileAt(int x, int y) const;
  void refreshDisplay();
  std::vector<cardlib::Card> &getPileReference(int pile_index);
  bool isValidDragSource(int pile_index, int card_index) const;
  double drag_offset_x_;
  double drag_offset_y_;

  std::unordered_map<std::string, cairo_surface_t *> card_surface_cache_;

  // Double buffering surface
  cairo_surface_t *buffer_surface_;
  cairo_t *buffer_cr_;

  // Methods for image caching
  void initializeCardCache();
  void cleanupCardCache();
  cairo_surface_t *getCardSurface(const cardlib::Card &card);
  cairo_surface_t *getCardBackSurface();

  void setupMenuBar();
  static void onNewGame(GtkWidget *widget, gpointer data);
  void restartGame();
  void promptForSeed();
  static void onQuit(GtkWidget *widget, gpointer data);
  static void onAbout(GtkWidget *widget, gpointer data);
  GtkWidget *vbox_; // Vertical box to hold menu and game area

  bool draw_three_mode_; // True for draw 3, false for draw 1
  bool tryMoveToFoundation(const cardlib::Card &card);
  void drawAnimatedCard(cairo_t *cr, const AnimatedCard &anim_card);
  void dealTestLayout();

  std::string settings_dir_;
  std::string custom_back_path_;
  bool loadSettings();
  void saveSettings();
  void initializeSettingsDir();
  bool setCustomCardBack(const std::string &path);

  bool loadDeck(const std::string &path);
  void cleanupResources();

  void resetToDefaultBack();
  void clearCustomBack();
  void refreshCardCache();

  static constexpr double EXPLOSION_THRESHOLD_MIN =
      0.3; // Minimum distance threshold (as percentage of screen height)
  static constexpr double EXPLOSION_THRESHOLD_MAX =
      0.7; // Maximum distance threshold (as percentage of screen height)

  void explodeCard(AnimatedCard &card);
  void updateCardFragments(AnimatedCard &card);
  void drawCardFragment(cairo_t *cr, const CardFragment &fragment);
  void startFoundationMoveAnimation(const cardlib::Card &card, int source_pile,
                                    int source_index, int target_pile);
  void updateFoundationMoveAnimation();
  static gboolean onFoundationMoveAnimationTick(gpointer data);

  bool stock_to_waste_animation_active_ = false;
  AnimatedCard stock_to_waste_card_;
  int stock_to_waste_timer_ = 0;
  std::vector<cardlib::Card> pending_waste_cards_;

  void startStockToWasteAnimation();
  void updateStockToWasteAnimation();
  static gboolean onStockToWasteAnimationTick(gpointer data);
  void completeStockToWasteAnimation();
  static void onToggleFullscreen(GtkWidget *widget, gpointer data);

  bool is_fullscreen_;
  static gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event,
                             gpointer data);
  void toggleFullscreen();

  int selected_pile_;     // Currently selected pile (-1 if none)
  int selected_card_idx_; // Index of selected card in the pile

  // Keyboard navigation
  void selectNextPile();
  void selectPreviousPile();
  void selectCardUp();
  void selectCardDown();
  void activateSelected();
  void highlightSelectedCard(cairo_t *cr);
  bool keyboard_navigation_active_ = false;
  bool tryMoveSelectedCard();
  bool keyboard_selection_active_ =
      false;                 // Flag for when a card is selected for movement
  int source_pile_ = -1;     // Source pile for keyboard moves
  int source_card_idx_ = -1; // Index of card in source pile

  void autoFinishGame();
  bool auto_finish_active_ = false;
  guint auto_finish_timer_id_ = 0;

  void processNextAutoFinishMove();
  static gboolean onAutoFinishTick(gpointer data);
  void resetKeyboardNavigation();

  std::string sounds_zip_path_;
  bool sound_enabled_;

  // Method to check if sound.zip exists and initialize sound system
  void checkAndInitializeSound();

  // Method to initialize sound system
  bool initializeAudio();

  // Method to load a specific sound from the ZIP archive
  bool loadSoundFromZip(GameSoundEvent event, const std::string &soundFileName);

  // Method to play a sound
  void playSound(GameSoundEvent event);

  // Method to clean up audio resources
  void cleanupAudio();

  unsigned int current_seed_;
  void drawEmptyPile(cairo_t *cr, int x, int y);

  void showHowToPlay();
  void showKeyboardShortcuts();
  void showDirectoryStructureDialog(const std::string &directory);
  void showMissingFileDialog(const std::string &filename, const std::string &details);
  void showErrorDialog(const std::string &title, const std::string &message);

  // Drawing-related methods
  void initializeOrResizeBuffer(int width, int height);
  void drawStockPile();
  void drawWastePile();
  void drawFoundationPiles();
  void drawFoundationDuringWinAnimation(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y);
  void drawNormalFoundationPile(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y);
  void drawTableauPiles();
  void drawTableauDuringDealAnimation(size_t pile_index, const std::vector<TableauCard> &pile, int x, int base_y);
  void drawNormalTableauPile(size_t pile_index, const std::vector<TableauCard> &pile, int x, int base_y);
  void drawAllAnimations();
  void drawDraggedCards();
  void drawWinAnimation();
  void drawDealAnimation();
  void dealMultiDeck();

  // Game mode (number of decks)
  GameMode current_game_mode_ = GameMode::STANDARD_KLONDIKE;
  void updateWindowTitle();

  // Replace the single deck with a MultiDeck
  cardlib::MultiDeck multi_deck_;
  
  // Add a method to switch game modes
  void switchGameMode(GameMode mode);
  
  // Initialization method for multiple decks
  void initializeMultiDeckGame();

  bool extractFileFromZip(const std::string &zipFilePath,
                          const std::string &fileName,
                          std::vector<uint8_t> &fileData);
};

#endif // SOLITAIRE_H
