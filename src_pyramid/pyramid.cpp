#include "pyramid.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <ctime>
#include <zip.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PyramidGame::PyramidGame()
    : game_area_(nullptr), gl_area_(nullptr), rendering_stack_(nullptr),
      buffer_surface_(nullptr), buffer_cr_(nullptr), draw_three_mode_(true),
      current_card_width_(BASE_CARD_WIDTH),
      current_card_height_(BASE_CARD_HEIGHT),
      current_card_spacing_(BASE_CARD_SPACING),
      current_vert_spacing_(BASE_VERT_SPACING), is_fullscreen_(false),
      selected_pile_(-1), selected_card_idx_(-1),
      keyboard_navigation_active_(false), keyboard_selection_active_(false),
      current_game_mode_(GameMode::PYRAMID_SINGLE), multi_deck_(1),
      sound_enabled_(true),
#ifdef __linux__
      rendering_engine_(RenderingEngine::CAIRO),
#else
      rendering_engine_(RenderingEngine::CAIRO),
#endif
      opengl_initialized_(false), cairo_initialized_(false),
      engine_switch_requested_(false), requested_engine_(RenderingEngine::CAIRO),
#ifdef _WIN32
      sounds_zip_path_("sounds.zip"),
#else
      sounds_zip_path_("sounds.zip"),
#endif
      current_seed_(0) {
  srand(time(NULL));
  current_seed_ = rand();
  initializeSettingsDir();
  loadEnginePreference();
  initializeRenderingEngine();
  loadSettings();
}

PyramidGame::~PyramidGame() {
  cleanupResources();
}

// ============================================================================
// GAME INITIALIZATION
// ============================================================================

void PyramidGame::initializeGame() {
  pyramid_.clear();
  stock_.clear();
  waste_.clear();

  selected_card_row_ = -1;
  selected_card_col_ = -1;
  card_selected_ = false;

  switch (current_game_mode_) {
    case GameMode::PYRAMID_SINGLE:
      multi_deck_ = cardlib::MultiDeck(1);
      break;
    case GameMode::PYRAMID_DOUBLE:
      multi_deck_ = cardlib::MultiDeck(2);
      break;
  }

  multi_deck_.shuffle(current_seed_);
  dealInitialPyramid();

  while (!multi_deck_.isEmpty()) {
    auto card = multi_deck_.drawCard();
    if (card.has_value()) {
      stock_.push_back(card.value());
    }
  }

  refreshDisplay();
}

void PyramidGame::dealInitialPyramid() {
  // Create pyramid with 7 rows (1, 2, 3, 4, 5, 6, 7 cards)
  for (int row = 0; row < 7; row++) {
    std::vector<PyramidCard> row_cards;
    for (int col = 0; col <= row; col++) {
      auto card_opt = multi_deck_.drawCard();
      if (card_opt.has_value()) {
        row_cards.push_back(PyramidCard(card_opt.value(), true, false));
      }
    }
    pyramid_.push_back(row_cards);
  }
}

void PyramidGame::restartGame() {
  initializeGame();
  if (rendering_engine_ == RenderingEngine::CAIRO) {
    gtk_widget_queue_draw(game_area_);
  }
#ifdef USEOPENGL
  else {
    gtk_widget_queue_draw(gl_area_);
  }
#endif
}

void PyramidGame::promptForSeed() {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Enter Game Seed", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
      "OK", GTK_RESPONSE_OK,
      "Cancel", GTK_RESPONSE_CANCEL, NULL);

  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *label =
      gtk_label_new("Enter a seed number (or leave blank for random):");
  gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 0);

  GtkWidget *entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 0);

  gtk_widget_show_all(dialog);

  gint result = gtk_dialog_run(GTK_DIALOG(dialog));

  if (result == GTK_RESPONSE_OK) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    if (text && strlen(text) > 0) {
      try {
        current_seed_ = std::stoul(text);
      } catch (...) {
        current_seed_ = static_cast<unsigned int>(time(nullptr));
      }
    } else {
      current_seed_ = static_cast<unsigned int>(time(nullptr));
    }
    restartGame();
  }

  gtk_widget_destroy(dialog);
}

// ============================================================================
// DRAWING - CAIRO
// ============================================================================

gboolean PyramidGame::onDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  int width = gtk_widget_get_allocated_width(widget);
  int height = gtk_widget_get_allocated_height(widget);

  game->game_area_width_ = width;
  game->game_area_height_ = height;
  game->updateCardDimensions(width, height);

  game->drawGame(cr, width, height);

  return FALSE;
}

void PyramidGame::drawGame(cairo_t *cr, int width, int height) {
  // Background
  cairo_set_source_rgb(cr, 0.0, 0.4, 0.0);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  // Title
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_set_font_size(cr, 20);
  cairo_move_to(cr, 20, 30);
  cairo_show_text(cr, "Pyramid Solitaire");

  // Stats
  cairo_set_font_size(cr, 14);
  cairo_move_to(cr, 20, 55);
  std::string stats = "Removed: " + std::to_string(countRemovedCards()) + "/28";
  cairo_show_text(cr, stats.c_str());

  // Pyramid
  drawPyramid(cr, width, height);

  // Stock and waste
  drawStockAndWaste(cr, width, height);

  // Instructions
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
  cairo_move_to(cr, 20, height - 20);
  cairo_show_text(cr, "Click pairs that sum to 13, Kings alone. Right-click to deselect.");
}

void PyramidGame::drawPyramid(cairo_t *cr, int width, int height) {
  int pyramid_start_y = 100;

  for (int row = 0; row < 7; row++) {
    int row_width = (row + 1) * (current_card_width_ + current_card_spacing_);
    int row_x = (width - row_width) / 2;

    for (int col = 0; col <= row; col++) {
      int card_x = row_x + col * (current_card_width_ + current_card_spacing_);
      int card_y = pyramid_start_y + row * (current_card_height_ + current_vert_spacing_);

      const PyramidCard &pcard = pyramid_[row][col];

      if (!pcard.removed) {
        drawPyramidCard(cr, pcard, card_x, card_y,
                       selected_card_row_ == row && selected_card_col_ == col);
      } else {
        // Empty space
        cairo_set_source_rgb(cr, 0.1, 0.3, 0.1);
        cairo_rectangle(cr, card_x, card_y, current_card_width_, current_card_height_);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.3, 0.5, 0.3);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, card_x, card_y, current_card_width_, current_card_height_);
        cairo_stroke(cr);
      }
    }
  }
}

void PyramidGame::drawPyramidCard(cairo_t *cr, const PyramidCard &card, int x,
                                   int y, bool is_selected) {
  if (card.face_up) {
    drawCard(cr, card.card, x, y, true);
  } else {
    drawCardBack(cr, x, y);
  }

  if (is_selected) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
    cairo_set_line_width(cr, 4.0);
    cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
    cairo_stroke(cr);
  }
}

void PyramidGame::drawStockAndWaste(cairo_t *cr, int width, int height) {
  int stock_x = width - current_card_width_ - current_card_spacing_;
  int stock_y = current_card_spacing_;

  // Stock pile
  if (!stock_.empty()) {
    drawCardBack(cr, stock_x, stock_y);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, stock_x + 5, stock_y + current_card_height_ + 15);
    std::string stock_text = "Stock: " + std::to_string(stock_.size());
    cairo_show_text(cr, stock_text.c_str());
  } else {
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_rectangle(cr, stock_x, stock_y, current_card_width_, current_card_height_);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_move_to(cr, stock_x + 20, stock_y + current_card_height_ / 2);
    cairo_show_text(cr, "Empty");
  }

  // Waste pile
  int waste_x = stock_x - current_card_width_ - current_card_spacing_;

  if (!waste_.empty()) {
    drawCard(cr, waste_.back(), waste_x, stock_y, true);
  } else {
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_rectangle(cr, waste_x, stock_y, current_card_width_, current_card_height_);
    cairo_stroke(cr);
  }
}

void PyramidGame::drawCard(cairo_t *cr, const cardlib::Card &card, int x, int y,
                            bool face_up) {
  if (!face_up) {
    drawCardBack(cr, x, y);
    return;
  }

  // Card background (white)
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
  cairo_fill(cr);

  // Border
  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
  cairo_set_line_width(cr, 2.0);
  cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
  cairo_stroke(cr);

  // Suit color
  bool is_red = (card.suit == cardlib::Suit::HEARTS ||
                 card.suit == cardlib::Suit::DIAMONDS);
  if (is_red) {
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
  } else {
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
  }

  cairo_set_font_size(cr, 18);

  // Rank text
  const char *rank_str = "";
  switch (card.rank) {
    case cardlib::Rank::ACE:
      rank_str = "A";
      break;
    case cardlib::Rank::TWO:
      rank_str = "2";
      break;
    case cardlib::Rank::THREE:
      rank_str = "3";
      break;
    case cardlib::Rank::FOUR:
      rank_str = "4";
      break;
    case cardlib::Rank::FIVE:
      rank_str = "5";
      break;
    case cardlib::Rank::SIX:
      rank_str = "6";
      break;
    case cardlib::Rank::SEVEN:
      rank_str = "7";
      break;
    case cardlib::Rank::EIGHT:
      rank_str = "8";
      break;
    case cardlib::Rank::NINE:
      rank_str = "9";
      break;
    case cardlib::Rank::TEN:
      rank_str = "10";
      break;
    case cardlib::Rank::JACK:
      rank_str = "J";
      break;
    case cardlib::Rank::QUEEN:
      rank_str = "Q";
      break;
    case cardlib::Rank::KING:
      rank_str = "K";
      break;
    case cardlib::Rank::JOKER:
      rank_str = "JO";
      break;
  }

  cairo_move_to(cr, x + 8, y + 25);
  cairo_show_text(cr, rank_str);

  // Suit text
  const char *suit_str = "";
  switch (card.suit) {
    case cardlib::Suit::HEARTS:
      suit_str = "♥";
      break;
    case cardlib::Suit::DIAMONDS:
      suit_str = "♦";
      break;
    case cardlib::Suit::CLUBS:
      suit_str = "♣";
      break;
    case cardlib::Suit::SPADES:
      suit_str = "♠";
      break;
    default:
      suit_str = "?";
      break;
  }

  cairo_move_to(cr, x + 8, y + 50);
  cairo_show_text(cr, suit_str);
}

void PyramidGame::drawCardBack(cairo_t *cr, int x, int y) {
  cairo_set_source_rgb(cr, 0.0, 0.2, 0.5);
  cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
  cairo_fill(cr);

  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_set_line_width(cr, 2.0);
  cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
  cairo_stroke(cr);

  // Pattern
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 7; j++) {
      int px = x + 15 + i * 15;
      int py = y + 15 + j * 18;
      cairo_arc(cr, px, py, 3, 0, 2 * M_PI);
      cairo_fill(cr);
    }
  }
}

// ============================================================================
// GAME LOGIC
// ============================================================================

void PyramidGame::selectPyramidCard(int row, int col) {
  if (pyramid_[row][col].removed || !isCardExposed(row, col)) {
    playSound(GameSoundEvent::NoMatch);
    return;
  }

  const cardlib::Card &card = pyramid_[row][col].card;

  if (!card_selected_) {
    selected_card_row_ = row;
    selected_card_col_ = col;
    card_selected_ = true;
    playSound(GameSoundEvent::CardFlip);
  } else {
    if (selected_card_row_ == row && selected_card_col_ == col) {
      // Same card clicked again
      if (canRemoveKing(card)) {
        removeKing(row, col);
        playSound(GameSoundEvent::CardPlace);
        if (checkWinCondition()) {
          startWinAnimation();
        }
      } else {
        playSound(GameSoundEvent::NoMatch);
      }
      selected_card_row_ = -1;
      selected_card_col_ = -1;
      card_selected_ = false;
    } else {
      // Different card clicked
      const cardlib::Card &selected = pyramid_[selected_card_row_][selected_card_col_].card;

      if (canRemovePair(selected, card)) {
        removePair(selected_card_row_, selected_card_col_, row, col);
        playSound(GameSoundEvent::CardPlace);

        if (checkWinCondition()) {
          startWinAnimation();
        }
      } else {
        playSound(GameSoundEvent::NoMatch);
      }
      selected_card_row_ = -1;
      selected_card_col_ = -1;
      card_selected_ = false;
    }
  }

  refreshDisplay();
}

void PyramidGame::selectWasteCard() {
  if (!waste_.empty()) {
    playSound(GameSoundEvent::CardFlip);
  }
}

void PyramidGame::handleStockPileClick() {
  if (stock_.empty()) {
    playSound(GameSoundEvent::NoMatch);
  } else {
    waste_.push_back(stock_.back());
    stock_.pop_back();
    playSound(GameSoundEvent::DealCard);
    refreshDisplay();
  }
}

void PyramidGame::removePair(int row1, int col1, int row2, int col2) {
  if (row1 >= 0 && row1 < 7 && col1 >= 0 && col1 <= row1 &&
      row2 >= 0 && row2 < 7 && col2 >= 0 && col2 <= row2) {
    pyramid_[row1][col1].removed = true;
    pyramid_[row2][col2].removed = true;
  }
}

void PyramidGame::removeKing(int row, int col) {
  if (row >= 0 && row < 7 && col >= 0 && col <= row) {
    pyramid_[row][col].removed = true;
  }
}

bool PyramidGame::isCardExposed(int row, int col) const {
  if (row < 0 || row >= 7 || col < 0 || col > row) {
    return false;
  }

  if (pyramid_[row][col].removed) {
    return false;
  }

  if (row == 6) {
    return true;
  }

  bool left_removed = pyramid_[row + 1][col].removed;
  bool right_removed = pyramid_[row + 1][col + 1].removed;

  return left_removed && right_removed;
}

bool PyramidGame::canRemovePair(const cardlib::Card &card1,
                                 const cardlib::Card &card2) const {
  return (getCardValue(card1) + getCardValue(card2)) == 13;
}

bool PyramidGame::canRemoveKing(const cardlib::Card &card) const {
  return card.rank == cardlib::Rank::KING;
}

int PyramidGame::getCardValue(const cardlib::Card &card) const {
  switch (card.rank) {
    case cardlib::Rank::ACE:
      return 1;
    case cardlib::Rank::TWO:
      return 2;
    case cardlib::Rank::THREE:
      return 3;
    case cardlib::Rank::FOUR:
      return 4;
    case cardlib::Rank::FIVE:
      return 5;
    case cardlib::Rank::SIX:
      return 6;
    case cardlib::Rank::SEVEN:
      return 7;
    case cardlib::Rank::EIGHT:
      return 8;
    case cardlib::Rank::NINE:
      return 9;
    case cardlib::Rank::TEN:
      return 10;
    case cardlib::Rank::JACK:
      return 11;
    case cardlib::Rank::QUEEN:
      return 12;
    case cardlib::Rank::KING:
      return 13;
    default:
      return 0;
  }
}

bool PyramidGame::checkWinCondition() const {
  for (const auto &row : pyramid_) {
    for (const auto &card : row) {
      if (!card.removed) {
        return false;
      }
    }
  }
  return true;
}

int PyramidGame::countRemovedCards() const {
  int count = 0;
  for (const auto &row : pyramid_) {
    for (const auto &card : row) {
      if (card.removed) {
        count++;
      }
    }
  }
  return count;
}

std::pair<int, int> PyramidGame::getPileAt(int x, int y) const {
  int pyramid_start_y = 100;

  for (int row = 0; row < 7; row++) {
    int row_width = (row + 1) * (current_card_width_ + current_card_spacing_);
    int row_x = (game_area_width_ - row_width) / 2;

    for (int col = 0; col <= row; col++) {
      int card_x = row_x + col * (current_card_width_ + current_card_spacing_);
      int card_y = pyramid_start_y + row * (current_card_height_ + current_vert_spacing_);

      if (x >= card_x && x < card_x + current_card_width_ &&
          y >= card_y && y < card_y + current_card_height_) {
        return {row * 10 + col + 2, 0};
      }
    }
  }

  int stock_x = game_area_width_ - current_card_width_ - current_card_spacing_;
  int stock_y = current_card_spacing_;

  if (x >= stock_x && x < stock_x + current_card_width_ &&
      y >= stock_y && y < stock_y + current_card_height_) {
    return {0, 0};
  }

  int waste_x = stock_x - current_card_width_ - current_card_spacing_;

  if (x >= waste_x && x < waste_x + current_card_width_ &&
      y >= stock_y && y < stock_y + current_card_height_) {
    return {1, 0};
  }

  return {-1, -1};
}

// ============================================================================
// UTILITY METHODS
// ============================================================================

void PyramidGame::refreshDisplay() {
  if (rendering_engine_ == RenderingEngine::CAIRO) {
    if (game_area_) {
      gtk_widget_queue_draw(game_area_);
    }
  }
#ifdef USEOPENGL
  else {
    if (gl_area_) {
      gtk_widget_queue_draw(gl_area_);
    }
  }
#endif
}

void PyramidGame::updateCardDimensions(int window_width, int window_height) {
  double scale = getScaleFactor(window_width, window_height);
  current_card_width_ = static_cast<int>(BASE_CARD_WIDTH * scale);
  current_card_height_ = static_cast<int>(BASE_CARD_HEIGHT * scale);
  current_card_spacing_ = static_cast<int>(BASE_CARD_SPACING * scale);
  current_vert_spacing_ = static_cast<int>(BASE_VERT_SPACING * scale);
}

double PyramidGame::getScaleFactor(int window_width, int window_height) const {
  double scale_x = static_cast<double>(window_width) / BASE_WINDOW_WIDTH;
  double scale_y = static_cast<double>(window_height) / BASE_WINDOW_HEIGHT;
  return std::min(scale_x, scale_y);
}

void PyramidGame::toggleFullscreen() {
  is_fullscreen_ = !is_fullscreen_;
  if (is_fullscreen_) {
    gtk_window_fullscreen(GTK_WINDOW(window_));
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
  }
}

// ============================================================================
// SETTINGS
// ============================================================================

void PyramidGame::initializeSettingsDir() {
#ifdef _WIN32
  const char *appdata = getenv("APPDATA");
  if (appdata) {
    std::string dir = std::string(appdata) + "\\PyramidSolitaire";
    _mkdir(dir.c_str());
  }
#else
  const char *home = getenv("HOME");
  if (home) {
    std::string dir = std::string(home) + "/.pyramid-solitaire";
    mkdir(dir.c_str(), 0755);
  }
#endif
}

void PyramidGame::saveSettings() {
  // TODO: Implement settings save (window size, game mode, etc.)
}

void PyramidGame::loadSettings() {
  // TODO: Implement settings load
}

// ============================================================================
// ANIMATION STUBS
// ============================================================================

void PyramidGame::startWinAnimation() {
  if (win_animation_active_) {
    return;
  }

  win_animation_active_ = true;
  cards_launched_ = 0;
  launch_timer_ = 0;
  animated_cards_.clear();

  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onWinAnimationTick, this);
}

void PyramidGame::stopWinAnimation() {
  if (!win_animation_active_) {
    return;
  }

  win_animation_active_ = false;

  if (animation_timer_id_ != 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  animated_cards_.clear();
  refreshDisplay();
}

gboolean PyramidGame::onWinAnimationTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->updateWinAnimation();

  if (game->win_animation_active_) {
    game->refreshDisplay();
    return TRUE;
  }

  return FALSE;
}

void PyramidGame::updateWinAnimation() {
  launch_timer_ += ANIMATION_INTERVAL;

  if (launch_timer_ > 30 && cards_launched_ < 28) {
    launchNextCard();
    launch_timer_ = 0;
  }

  for (auto &card : animated_cards_) {
    if (card.active) {
      card.velocity_y += GRAVITY;
      card.x += card.velocity_x;
      card.y += card.velocity_y;
      card.rotation += card.rotation_velocity;

      if (card.y > game_area_height_ + 100) {
        card.active = false;
      }
    }
  }

  bool any_active = false;
  for (const auto &card : animated_cards_) {
    if (card.active) {
      any_active = true;
      break;
    }
  }

  if (!any_active && cards_launched_ >= 28) {
    win_animation_active_ = false;
  }
}

void PyramidGame::launchNextCard() {
  if (cards_launched_ >= 28) {
    return;
  }

  int row = cards_launched_ / 7;
  int col = cards_launched_ % 7;

  if (row < 7 && col <= row) {
    AnimatedCard acard;
    acard.card = pyramid_[row][col].card;
    acard.face_up = true;
    acard.active = true;
    acard.exploded = false;

    int pyramid_start_y = 100;
    int row_width = (row + 1) * (current_card_width_ + current_card_spacing_);
    int row_x = (game_area_width_ - row_width) / 2;

    acard.x = row_x + col * (current_card_width_ + current_card_spacing_);
    acard.y = pyramid_start_y + row * (current_card_height_ + current_vert_spacing_);

    acard.velocity_x = (rand() % 20 - 10) * 0.5;
    acard.velocity_y = -15;
    acard.rotation = 0;
    acard.rotation_velocity = (rand() % 10 - 5) * 0.2;

    animated_cards_.push_back(acard);
  }

  cards_launched_++;
}

void PyramidGame::explodeCard(AnimatedCard &card) {
  // TODO: Implement card explosion effect
}

void PyramidGame::updateCardFragments(AnimatedCard &card) {
  // TODO: Implement card fragment updates
}

void PyramidGame::startDealAnimation() {
  // TODO: Implement deal animation
}

void PyramidGame::updateDealAnimation() {
  // TODO: Implement deal animation updates
}

void PyramidGame::dealNextCard() {
  // TODO: Implement dealing next card
}

void PyramidGame::completeDeal() {
  // TODO: Implement deal completion
}

void PyramidGame::stopDealAnimation() {
  // TODO: Implement stop deal animation
}

gboolean PyramidGame::onDealAnimationTick(gpointer data) {
  return FALSE;
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

gboolean PyramidGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

  if (game->win_animation_active_ || game->deal_animation_active_) {
    return TRUE;
  }

  if (event->button == 1) {
    auto [pile_id, _] = game->getPileAt(event->x, event->y);

    if (pile_id == 0) {
      game->handleStockPileClick();
      return TRUE;
    }

    if (pile_id == 1) {
      if (!game->waste_.empty()) {
        game->selectWasteCard();
      }
      return TRUE;
    }

    if (pile_id >= 2) {
      int pyramid_id = pile_id - 2;
      int row = pyramid_id / 10;
      int col = pyramid_id % 10;

      if (row >= 0 && row < 7 && col >= 0 && col <= row) {
        game->selectPyramidCard(row, col);
      }
      return TRUE;
    }
  } else if (event->button == 3) {
    game->selected_card_row_ = -1;
    game->selected_card_col_ = -1;
    game->card_selected_ = false;
    game->refreshDisplay();
    return TRUE;
  }

  return TRUE;
}

gboolean PyramidGame::onButtonRelease(GtkWidget *widget, GdkEventButton *event,
                                       gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->keyboard_navigation_active_ = false;

  return TRUE;
}

gboolean PyramidGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                      gpointer data) {
  return TRUE;
}

gboolean PyramidGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                  gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  if (event->keyval == GDK_KEY_Escape) {
    game->selected_card_row_ = -1;
    game->selected_card_col_ = -1;
    game->card_selected_ = false;
    game->refreshDisplay();
  }

  return FALSE;
}

// ============================================================================
// MENU CALLBACKS
// ============================================================================

void PyramidGame::onNewGame(GtkWidget *widget, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->restartGame();
}

void PyramidGame::onQuit(GtkWidget *widget, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  gtk_widget_destroy(game->window_);
}

void PyramidGame::onAbout(GtkWidget *widget, gpointer data) {
  GtkWidget *dialog = gtk_about_dialog_new();
  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Pyramid Solitaire");
  gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0");
  gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "(c) 2024");
  gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
                                "A classic solitaire card game.");

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::onToggleFullscreen(GtkWidget *widget, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->toggleFullscreen();
}

// ============================================================================
// ENGINE AND RENDERING
// ============================================================================

bool PyramidGame::isOpenGLSupported() const {
#ifndef USEOPENGL
  return false;
#else
  return true;
#endif
}

bool PyramidGame::setRenderingEngine(RenderingEngine engine) {
  if (rendering_engine_ == engine) {
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
      std::cerr << "Using Cairo rendering" << std::endl;
      return true;
    case RenderingEngine::OPENGL:
      std::cerr << "OpenGL rendering requested" << std::endl;
      return true;
    default:
      rendering_engine_ = RenderingEngine::CAIRO;
      cairo_initialized_ = true;
      return true;
  }
}

bool PyramidGame::switchRenderingEngine(RenderingEngine newEngine) {
  if (newEngine == rendering_engine_) {
    return true;
  }

  rendering_engine_ = newEngine;
  return true;
}

void PyramidGame::cleanupRenderingEngine() {
  // Cleanup code
}

std::string PyramidGame::getRenderingEngineName() const {
  return (rendering_engine_ == RenderingEngine::CAIRO) ? "Cairo" : "OpenGL";
}

void PyramidGame::printEngineInfo() {
  std::cout << "Rendering Engine: " << getRenderingEngineName() << std::endl;
}

void PyramidGame::addEngineSelectionMenu(GtkWidget *menubar) {
  // TODO: Implement engine selection menu
}

void PyramidGame::saveEnginePreference() {
  // TODO: Save preference
}

void PyramidGame::loadEnginePreference() {
  // TODO: Load preference
}

void PyramidGame::renderFrame() {
  // TODO: Implement frame rendering
}

// ============================================================================
// SOUND SYSTEM
// ============================================================================

bool PyramidGame::setSoundsZipPath(const std::string &path) {
  sounds_zip_path_ = path;
  saveSettings();
  return true;
}

void PyramidGame::checkAndInitializeSound() {
  if (sound_enabled_) {
    initializeAudio();
  }
}

bool PyramidGame::initializeAudio() {
  if (!sound_enabled_) {
    return false;
  }

  std::cerr << "Audio initialization stub" << std::endl;
  return true;
}

bool PyramidGame::loadSoundFromZip(GameSoundEvent event,
                                    const std::string &soundFileName) {
  return true;
}

void PyramidGame::playSound(GameSoundEvent event) {
  if (!sound_enabled_) {
    return;
  }

  // Sound playback stub
}

bool PyramidGame::extractFileFromZip(const std::string &zipFilePath,
                                      const std::string &fileName,
                                      std::vector<uint8_t> &fileData) {
  return false;
}

void PyramidGame::cleanupAudio() {
  // Cleanup audio
}

// ============================================================================
// RESOURCE CLEANUP
// ============================================================================

void PyramidGame::cleanupCardCache() {
  for (auto &pair : card_cache_) {
    if (pair.second) {
      cairo_surface_destroy(pair.second);
    }
  }
  card_cache_.clear();
}

void PyramidGame::cleanupResources() {
  stopWinAnimation();
  cleanupCardCache();
  cleanupAudio();

  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
    buffer_cr_ = nullptr;
  }

  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
    buffer_surface_ = nullptr;
  }
}

bool PyramidGame::loadDeck(const std::string &path) {
  // TODO: Implement deck loading
  return true;
}

void PyramidGame::clearAndRebuildCaches() {
  cleanupCardCache();
  cache_dirty_ = false;
}

// ============================================================================
// UI HELPERS
// ============================================================================

void PyramidGame::showHowToPlay() {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "How to Play", GTK_WINDOW(window_), GTK_DIALOG_MODAL, "Close",
      GTK_RESPONSE_OK, NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *label = gtk_label_new(
      "Pyramid Solitaire\n\n"
      "Remove all 28 cards from the pyramid.\n\n"
      "Rules:\n"
      "- Click pairs of cards that sum to 13\n"
      "- Kings can be removed alone\n"
      "- Only exposed cards can be selected\n"
      "- Cards are exposed when both below them are gone\n\n"
      "Bottom row cards are always exposed.\n");

  gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
  gtk_widget_show_all(dialog);

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showKeyboardShortcuts() {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Keyboard Shortcuts", GTK_WINDOW(window_), GTK_DIALOG_MODAL, "Close",
      GTK_RESPONSE_OK, NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *label = gtk_label_new(
      "Keyboard Shortcuts:\n\n"
      "Esc - Deselect card\n"
      "F11 - Toggle fullscreen\n");

  gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
  gtk_widget_show_all(dialog);

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void PyramidGame::showGameStats() {
  // TODO: Implement game stats display
}

void PyramidGame::showErrorDialog(const std::string &title,
                                   const std::string &message) {
  GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window_),
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                              "%s", message.c_str());
  gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

// ============================================================================
// KEYBOARD NAVIGATION STUBS
// ============================================================================

void PyramidGame::selectNextCard() {
  // TODO: Implement
}

void PyramidGame::selectPreviousCard() {
  // TODO: Implement
}

void PyramidGame::activateSelected() {
  // TODO: Implement
}

void PyramidGame::resetKeyboardNavigation() {
  // TODO: Implement
}

// ============================================================================
// OPENGL STUBS
// ============================================================================

#ifdef USEOPENGL
gboolean PyramidGame::onGLRealize(GtkGLArea *area, gpointer data) {
  return TRUE;
}

gboolean PyramidGame::onGLRender(GtkGLArea *area, GdkGLContext *context,
                                  gpointer data) {
  return TRUE;
}

void PyramidGame::drawGame_gl() {}
void PyramidGame::drawPyramid_gl(GLuint shaderProgram, GLuint VAO) {}
void PyramidGame::drawStockAndWaste_gl(GLuint shaderProgram, GLuint VAO) {}
void PyramidGame::highlightSelectedCard_gl(GLuint shaderProgram, GLuint VAO) {}
void PyramidGame::startWinAnimation_gl() {}
void PyramidGame::updateWinAnimation_gl() {}
void PyramidGame::stopWinAnimation_gl() {}
void PyramidGame::launchNextCard_gl() {}
void PyramidGame::explodeCard_gl(AnimatedCard &card) {}
void PyramidGame::updateCardFragments_gl(AnimatedCard &card) {}
void PyramidGame::drawWinAnimation_gl(GLuint shaderProgram, GLuint VAO) {}
void PyramidGame::startDealAnimation_gl() {}
void PyramidGame::updateDealAnimation_gl() {}
void PyramidGame::dealNextCard_gl() {}
void PyramidGame::completeDeal_gl() {}
void PyramidGame::stopDealAnimation_gl() {}
void PyramidGame::drawDealAnimation_gl(GLuint shaderProgram, GLuint VAO) {}
static gboolean onDealAnimationTick_gl(gpointer data) { return FALSE; }

GLuint PyramidGame::setupShaders_gl() { return 0; }
GLuint PyramidGame::setupCardQuadVAO_gl() { return 0; }
bool PyramidGame::initializeCardTextures_gl() { return true; }
bool PyramidGame::loadCardTexture_gl(const std::string &cardKey,
                                     const cardlib::Card &card) {
  return true;
}
bool PyramidGame::initializeOpenGLResources() { return true; }
void PyramidGame::cleanupOpenGLResources_gl() {}
bool PyramidGame::validateOpenGLContext() { return true; }
bool PyramidGame::reloadCustomCardBackTexture_gl() { return true; }
GLuint PyramidGame::loadTextureFromMemory(const std::vector<unsigned char> &data) {
  return 0;
}
void PyramidGame::renderFrame_gl() {}
#endif

// ============================================================================
// MAIN RUN METHOD
// ============================================================================

void PyramidGame::run(int argc, char **argv) {
  gtk_init(&argc, &argv);

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Pyramid Solitaire");
  gtk_window_set_default_size(GTK_WINDOW(window_), BASE_WINDOW_WIDTH,
                              BASE_WINDOW_HEIGHT);

  vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window_), vbox_);

  // Menu bar
  GtkWidget *menubar = gtk_menu_bar_new();

  GtkWidget *file_menu_item = gtk_menu_item_new_with_label("Game");
  GtkWidget *file_menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);

  GtkWidget *new_game_item = gtk_menu_item_new_with_label("New Game");
  g_signal_connect(new_game_item, "activate", G_CALLBACK(onNewGame), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_game_item);

  GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
  g_signal_connect(quit_item, "activate", G_CALLBACK(onQuit), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_menu_item);

  GtkWidget *help_menu_item = gtk_menu_item_new_with_label("Help");
  GtkWidget *help_menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_menu_item), help_menu);

  GtkWidget *about_item = gtk_menu_item_new_with_label("About");
  g_signal_connect(about_item, "activate", G_CALLBACK(onAbout), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_menu_item);

  gtk_box_pack_start(GTK_BOX(vbox_), menubar, FALSE, FALSE, 0);

  // Game area
  game_area_ = gtk_drawing_area_new();
  gtk_widget_set_can_focus(game_area_, TRUE);
  g_signal_connect(game_area_, "draw", G_CALLBACK(onDraw), this);
  g_signal_connect(game_area_, "button-press-event", G_CALLBACK(onButtonPress),
                   this);
  g_signal_connect(game_area_, "button-release-event",
                   G_CALLBACK(onButtonRelease), this);
  g_signal_connect(game_area_, "motion-notify-event",
                   G_CALLBACK(onMotionNotify), this);
  g_signal_connect(game_area_, "key-press-event", G_CALLBACK(onKeyPress), this);

  gtk_widget_add_events(game_area_,
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);

  gtk_box_pack_start(GTK_BOX(vbox_), game_area_, TRUE, TRUE, 0);

  // Signal handling
  g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  // Initialize game
  initializeGame();
  checkAndInitializeSound();

  gtk_widget_show_all(window_);

  gtk_main();
}

void PyramidGame::dealTestLayout() {
  // TODO: Implement for testing
}
