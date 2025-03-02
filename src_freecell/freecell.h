#ifndef FREECELL_H
#define FREECELL_H

#include "cardlib.h"
#include <gtk/gtk.h>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

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

class FreecellGame {
public:
  FreecellGame();
  ~FreecellGame();
  
  bool setSoundsZipPath(const std::string &path);
  void run(int argc, char **argv);

private:

  bool auto_finish_active_ = false;
  guint auto_finish_timer_id_ = 0;
  std::vector<std::vector<bool>> animated_foundation_cards_;
  // Foundation move animation fields
  bool foundation_move_animation_active_ = false;
  AnimatedCard foundation_move_card_;
  int foundation_source_pile_ = -1;
  int foundation_target_pile_ = -1;
  double foundation_move_timer_ = 0;
  static constexpr double FOUNDATION_MOVE_SPEED = 0.4;

  // Auto-finish methods
  void autoFinishGame();
  void processNextAutoFinishMove();
  static gboolean onAutoFinishTick(gpointer data);
  
  // Foundation move animation methods
  void startFoundationMoveAnimation(const cardlib::Card &card, int source_pile, int source_index, int target_pile);
  void updateFoundationMoveAnimation();
  static gboolean onFoundationMoveAnimationTick(gpointer data);

  // Game state
  static constexpr int BASE_WINDOW_WIDTH = 1024;
  static constexpr int BASE_WINDOW_HEIGHT = 768;
  static constexpr int BASE_CARD_WIDTH = 100;
  static constexpr int BASE_CARD_HEIGHT = 145;
  static constexpr int BASE_CARD_SPACING = 20;
  static constexpr int BASE_VERT_SPACING = 25;
  int drag_source_card_idx_;
  std::vector<cardlib::Card> drag_cards_;
  // Current dynamic dimensions
  int current_card_width_;
  int current_card_height_;
  int current_card_spacing_;
  int current_vert_spacing_;
  
  // Card dimensions handler
  void updateCardDimensions(int window_width, int window_height);
  double getScaleFactor(int window_width, int window_height) const;
  std::vector<bool> animated_freecell_cards_;
  // Win animation fields
  bool win_animation_active_ = false;
  std::vector<AnimatedCard> animated_cards_;
  guint animation_timer_id_ = 0;
  static constexpr double GRAVITY = 0.8;
  static constexpr double BOUNCE_FACTOR = -0.7;
  static constexpr int ANIMATION_INTERVAL = 16; // ~60 FPS
  int cards_launched_ = 0;
  double launch_timer_ = 0;
  void stopWinAnimation();
  void startWinAnimation();
  void updateWinAnimation();
  void launchNextCard();
  static gboolean onAnimationTick(gpointer data);
  
  // Deal animation fields
  bool deal_animation_active_ = false;
  std::vector<AnimatedCard> deal_cards_;
  int cards_dealt_ = 0;
  double deal_timer_ = 0;
  static constexpr double DEAL_INTERVAL = 30; // Time between dealing cards (ms)
  static constexpr double DEAL_SPEED = 1.3;   // Speed multiplier for dealing
  
  // Deal animation methods
  void startDealAnimation();
  void updateDealAnimation();
  static gboolean onDealAnimationTick(gpointer data);
  void dealNextCard();
  void completeDeal();
  
  // Game components
  cardlib::Deck deck_;
  std::vector<std::optional<cardlib::Card>> freecells_; // 4 Free cells for temporary storage
  std::vector<std::vector<cardlib::Card>> foundation_; // 4 piles for aces (one per suit)
  std::vector<std::vector<cardlib::Card>> tableau_;    // 8 tableau columns
  std::vector<std::vector<cardlib::Card>> freecell_animation_cards_;
  // Drawing methods
  void drawCard(cairo_t *cr, int x, int y, const cardlib::Card *card);
  void drawEmptyPile(cairo_t *cr, int x, int y);
  void drawAnimatedCard(cairo_t *cr, const AnimatedCard &anim_card);
  void highlightSelectedCard(cairo_t *cr); // Added for keyboard navigation
  
  // Drag and drop state
  bool dragging_;
  std::optional<cardlib::Card> drag_card_;
  int drag_source_pile_;
  int drag_start_x_, drag_start_y_;
  double drag_offset_x_;
  double drag_offset_y_;
  
  // GTK widgets
  GtkWidget *window_;
  GtkWidget *game_area_;
  GtkWidget *vbox_; // Vertical box to hold menu and game area
  
  // Initialize game
  void initializeGame();
  void deal();
  
  // UI setup
  void setupWindow();
  void setupGameArea();
  void setupMenuBar();
  
  // Event handlers
  static gboolean onDraw(GtkWidget *widget, cairo_t *cr, gpointer data);
  static gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer data);
  static gboolean onMotionNotify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
  static gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data);
  
  // Menu handlers
  static void onNewGame(GtkWidget *widget, gpointer data);
  static void onQuit(GtkWidget *widget, gpointer data);
  static void onAbout(GtkWidget *widget, gpointer data);
  static void onToggleFullscreen(GtkWidget *widget, gpointer data);
  void setupEasyGame();
  // Helper functions
  void refreshDisplay();
  
  // Card image caching
  std::unordered_map<std::string, cairo_surface_t *> card_surface_cache_;
  void initializeCardCache();
  void cleanupCardCache();
  
  // Double buffering
  cairo_surface_t *buffer_surface_;
  cairo_t *buffer_cr_;
  
  // Settings and customization
  std::string settings_dir_;
  bool loadSettings();
  void saveSettings();
  void initializeSettingsDir();
  
  // Fullscreen mode
  bool is_fullscreen_;
  void toggleFullscreen();
  
  // Keyboard navigation
  int selected_pile_;     // Currently selected pile (-1 if none)
  int selected_card_idx_; // Index of selected card in the pile
  bool keyboard_navigation_active_;
  bool keyboard_selection_active_;
  int source_pile_;
  int source_card_idx_;
  
  // Keyboard navigation methods
  void selectNextPile();
  void selectPreviousPile();
  void selectCardUp();
  void selectCardDown();
  void activateSelected();
  void resetKeyboardNavigation();
  bool tryMoveSelectedCard();
  bool canSelectForMove();
  bool isCardPlayable();
  
  std::pair<int, int> getPileAt(int x, int y) const;
  
  // Card movement helpers
  bool tryMoveFromFreecell();
  bool tryMoveFromFoundation();
  bool tryMoveFromTableau();
  bool canMoveToFoundation(const cardlib::Card& card, int foundation_idx) const;
  bool canMoveToTableau(const cardlib::Card& card, int tableau_idx) const;
  bool canMoveTableauStack(const std::vector<cardlib::Card>& cards, int tableau_idx) const;
  bool isValidTableauSequence(const std::vector<cardlib::Card>& cards) const;
  bool isCardRed(const cardlib::Card& card) const;
  int findFirstPlayableCard(int tableau_idx);
  bool autoFinishMoves();

  // Sound system
  std::string sounds_zip_path_;
  bool sound_enabled_;
  bool initializeAudio();
  bool loadSoundFromZip(GameSoundEvent event, const std::string &soundFileName);
  void playSound(GameSoundEvent event);
  void cleanupAudio();
  bool isValidDragSource(int pile_index, int card_index) const;
  bool checkWinCondition() const;

  bool canMoveToFoundation(const cardlib::Card& card, int foundation_idx);
  bool canMoveToTableau(const cardlib::Card& card, int tableau_idx);
  bool isValidTableauSequence(const std::vector<cardlib::Card>& cards);
  bool isCardRed(const cardlib::Card&);
  bool canMoveTableauStack(const std::vector<cardlib::Card>& cards, int tableau_idx);
  bool handleSpacebarAction();
  // Helper function to extract files from ZIP
  bool extractFileFromZip(const std::string &zipFilePath,
                         const std::string &fileName,
                         std::vector<uint8_t> &fileData);

  unsigned int current_seed_;

  void promptForSeed();
  void restartGame();


  void explodeCard(AnimatedCard&);
  void updateCardFragments(AnimatedCard &card);
  void drawCardFragment(cairo_t *cr, const CardFragment &fragment);                   
  cairo_surface_t* getCardSurface(const cardlib::Card& card);

  void launchCardFromFreecell();

  void initializeDrawBuffer(int width, int height);
  void drawFreecells();
  void drawFoundationPiles();
  void drawTableau();
  void drawTableauDuringDealAnimation(int column_index, int x, int tableau_y);
  void drawNormalTableauColumn(int column_index, int x, int tableau_y);
  void drawDraggedCards();
  void drawAnimations();
  void drawWinAnimation();

  // For drawing allocation
  GtkAllocation allocation;
};

#endif // FREECELL_H
