#include "solitaire.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>  // For Sleep function on Windows
#include <shlobj.h>
#include <appmodel.h>
#include <vector>
#else
#include <unistd.h>   // For usleep function on Unix/Linux
#endif


#ifdef _WIN32

std::string getPackagePath() {
    UINT32 length = 0;
    LONG rc = GetCurrentPackagePath(&length, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
        return "";
    }

    std::vector<wchar_t> buffer(length);
    rc = GetCurrentPackagePath(&length, buffer.data());
    if (rc != ERROR_SUCCESS) {
        return "";
    }

    // Convert wide string to UTF‑8
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, nullptr, 0, nullptr, nullptr);
    std::string path(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, &path[0], size_needed, nullptr, nullptr);

    return path;
}

#endif

SolitaireGame::SolitaireGame()
    : dragging_(false), drag_source_(nullptr), drag_source_pile_(-1),
      window_(nullptr), game_area_(nullptr), buffer_surface_(nullptr),
      buffer_cr_(nullptr), draw_three_mode_(true),
      current_card_width_(BASE_CARD_WIDTH),
      current_card_height_(BASE_CARD_HEIGHT),
      current_card_spacing_(BASE_CARD_SPACING),
      current_vert_spacing_(BASE_VERT_SPACING), is_fullscreen_(false),
      selected_pile_(-1), selected_card_idx_(-1),
      keyboard_navigation_active_(false), keyboard_selection_active_(false),
      source_pile_(-1), source_card_idx_(-1),
      current_game_mode_(GameMode::STANDARD_KLONDIKE),
      multi_deck_(1), // Initialize with 1 deck
      sound_enabled_(true),           // Set sound to enabled by default
#ifdef WIN32
      sounds_zip_path_(getPackagePath() + "\\sound.zip"),
#else
      sounds_zip_path_("sound.zip"),
#endif
      current_seed_(0) { // Initialize to 0 temporarily
  srand(time(NULL));  // Seed the random number generator with current time
  current_seed_ = rand();  
  initializeSettingsDir();
  loadSettings();
}

SolitaireGame::~SolitaireGame() {
  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
  }
  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
  }
  cleanupAudio();
}

void SolitaireGame::checkAndInitializeSound() {
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



void SolitaireGame::run(int argc, char **argv) {
  gtk_init(&argc, &argv);
  setupWindow();
  initializeGame();  // Initialize game after GTK is ready and window exists
  setupGameArea();
  gtk_main();
}

void SolitaireGame::initializeGame() {
  if (current_game_mode_ == GameMode::STANDARD_KLONDIKE) {
    // Original single-deck initialization
    try {
      // Try to find cards.zip in several common locations
#ifdef _WIN32
      const std::vector<std::string> paths = {getPackagePath() + "\\cards.zip"};
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
}

bool SolitaireGame::isValidDragSource(int pile_index, int card_index) const {
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

std::vector<cardlib::Card> &SolitaireGame::getPileReference(int pile_index) {
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

void SolitaireGame::drawCard(cairo_t *cr, int x, int y,
                             const cardlib::Card *card, bool face_up) {
  if (face_up && card) {

    std::string key = std::to_string(static_cast<int>(card->suit)) +
                      std::to_string(static_cast<int>(card->rank));
    auto it = card_surface_cache_.find(key);

    if (it == card_surface_cache_.end()) {
      if (auto img = deck_.getCardImage(*card)) {
        GError *error = nullptr;
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

        if (!gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(),
                                     &error)) {
          if (error)
            g_error_free(error);
          g_object_unref(loader);
          return;
        }

        if (!gdk_pixbuf_loader_close(loader, &error)) {
          if (error)
            g_error_free(error);
          g_object_unref(loader);
          return;
        }

        GdkPixbuf *original_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (original_pixbuf) {
          GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(
              original_pixbuf,
              current_card_width_, // Use current dimensions
              current_card_height_, GDK_INTERP_BILINEAR);

          if (scaled_pixbuf) {
            cairo_surface_t *surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
            cairo_t *surface_cr = cairo_create(surface);

            gdk_cairo_set_source_pixbuf(surface_cr, scaled_pixbuf, 0, 0);
            cairo_paint(surface_cr);
            cairo_destroy(surface_cr);

            card_surface_cache_[key] = surface;

            g_object_unref(scaled_pixbuf);
          }
        }
        g_object_unref(loader);

        it = card_surface_cache_.find(key);
      }
    }

    if (it != card_surface_cache_.end()) {
      // Scale the surface to the current card dimensions
      cairo_save(cr);
      cairo_scale(cr,
                  (double)current_card_width_ /
                      cairo_image_surface_get_width(it->second),
                  (double)current_card_height_ /
                      cairo_image_surface_get_height(it->second));
      cairo_set_source_surface(cr, it->second,
                               x * cairo_image_surface_get_width(it->second) /
                                   current_card_width_,
                               y * cairo_image_surface_get_height(it->second) /
                                   current_card_height_);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  } else {
    auto custom_it = card_surface_cache_.find("custom_back");
    auto default_it = card_surface_cache_.find("back");
    cairo_surface_t *back_surface = nullptr;

    if (!custom_back_path_.empty() && custom_it != card_surface_cache_.end()) {
      back_surface = custom_it->second;
    } else if (default_it != card_surface_cache_.end()) {
      back_surface = default_it->second;
    }

    if (back_surface) {
      // Scale the surface to the current card dimensions
      cairo_save(cr);
      cairo_scale(cr,
                  (double)current_card_width_ /
                      cairo_image_surface_get_width(back_surface),
                  (double)current_card_height_ /
                      cairo_image_surface_get_height(back_surface));
      cairo_set_source_surface(
          cr, back_surface,
          x * cairo_image_surface_get_width(back_surface) / current_card_width_,
          y * cairo_image_surface_get_height(back_surface) /
              current_card_height_);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      // Draw a placeholder rectangle if no back image is available
      cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
      cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
      cairo_stroke(cr);
    }
  }
}

void SolitaireGame::deal() {
  // Clear all piles first
  stock_.clear();
  waste_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset foundation and tableau
  foundation_.resize(4);
  tableau_.resize(7);

  // Deal to tableau - i represents the pile number (0-6)
  for (int i = 0; i < 7; i++) {
    // For each pile i, deal i cards face down
    for (int j = 0; j < i; j++) {
      if (auto card = deck_.drawCard()) {
        tableau_[i].emplace_back(*card, false); // face down
        //playSound(GameSoundEvent::CardFlip);
      }
    }
    // Deal one card face up at the end
    if (auto card = deck_.drawCard()) {
      tableau_[i].emplace_back(*card, true); // face up
      //playSound(GameSoundEvent::CardFlip);
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

  // Start the deal animation
  startDealAnimation();
}

void SolitaireGame::flipTopTableauCard(int pile_index) {
  if (pile_index < 0 || pile_index >= static_cast<int>(tableau_.size())) {
    return;
  }

  auto &pile = tableau_[pile_index];
  if (!pile.empty() && !pile.back().face_up) {
    pile.back().face_up = true;
    // playSound(GameSoundEvent::CardFlip);
  }
}

GtkWidget *SolitaireGame::createCardWidget(const cardlib::Card &card,
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

std::vector<cardlib::Card> SolitaireGame::getTableauCardsAsCards(
    const std::vector<TableauCard> &tableau_cards, int start_index) {
  std::vector<cardlib::Card> cards;
  for (size_t i = start_index; i < tableau_cards.size(); i++) {
    if (tableau_cards[i].face_up) {
      cards.push_back(tableau_cards[i].card);
    }
  }
  return cards;
}

std::pair<int, int> SolitaireGame::getPileAt(int x, int y) const {
  // Check stock pile
  if (x >= current_card_spacing_ &&
      x <= current_card_spacing_ + current_card_width_ &&
      y >= current_card_spacing_ &&
      y <= current_card_spacing_ + current_card_height_) {
    return {0, stock_.empty() ? -1 : 0};
  }

  // Check waste pile
  if (x >= 2 * current_card_spacing_ + current_card_width_ &&
      x <= 2 * current_card_spacing_ + 2 * current_card_width_ &&
      y >= current_card_spacing_ &&
      y <= current_card_spacing_ + current_card_height_) {
    return {1, waste_.empty() ? -1 : static_cast<int>(waste_.size() - 1)};
  }

  // Check foundation piles - using foundation_.size() instead of hardcoded limit
  int foundation_x = 3 * (current_card_width_ + current_card_spacing_);
  for (int i = 0; i < foundation_.size(); i++) {
    if (x >= foundation_x && x <= foundation_x + current_card_width_ &&
        y >= current_card_spacing_ &&
        y <= current_card_spacing_ + current_card_height_) {
      return {2 + i, foundation_[i].empty()
                         ? -1
                         : static_cast<int>(foundation_[i].size() - 1)};
    }
    foundation_x += current_card_width_ + current_card_spacing_;
  }

  // Calculate first tableau index
  int first_tableau_index = 2 + foundation_.size();

  // Check tableau piles - check from top card down
  int tableau_y =
      current_card_spacing_ + current_card_height_ + current_vert_spacing_;
  for (int i = 0; i < tableau_.size(); i++) {
    int pile_x = current_card_spacing_ +
                 i * (current_card_width_ + current_card_spacing_);
    if (x >= pile_x && x <= pile_x + current_card_width_) {
      const auto &pile = tableau_[i];
      if (pile.empty() && y >= tableau_y &&
          y <= tableau_y + current_card_height_) {
        return {first_tableau_index + i, -1};
      }

      // Check cards from top to bottom
      for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
        int card_y = tableau_y + j * current_vert_spacing_;
        if (y >= card_y && y <= card_y + current_card_height_) {
          if (pile[j].face_up) {
            return {first_tableau_index + i, j};
          }
          break; // Hit a face-down card, stop checking
        }
      }
    }
  }

  return {-1, -1};
}

bool SolitaireGame::canMoveToPile(const std::vector<cardlib::Card> &cards,
                                  const std::vector<cardlib::Card> &target,
                                  bool is_foundation) const {

  if (cards.empty())
    return false;

  const auto &moving_card = cards[0];

  // Foundation pile rules
  if (is_foundation) {
    // Only single cards can go to foundation
    if (cards.size() != 1) {
      return false;
    }

    // For empty foundation, only accept aces
    if (target.empty()) {
      return moving_card.rank == cardlib::Rank::ACE;
    }

    // For non-empty foundation, must be same suit and next rank up
    const auto &target_card = target.back();
    return moving_card.suit == target_card.suit &&
           static_cast<int>(moving_card.rank) ==
               static_cast<int>(target_card.rank) + 1;
  }

  // Tableau pile rules
  if (target.empty()) {
    // Only kings can go to empty tableau spots
    return static_cast<int>(moving_card.rank) ==
           static_cast<int>(cardlib::Rank::KING);
  }

  const auto &target_card = target.back();

  // Must be opposite color and one rank lower
  bool opposite_color = ((target_card.suit == cardlib::Suit::HEARTS ||
                          target_card.suit == cardlib::Suit::DIAMONDS) !=
                         (moving_card.suit == cardlib::Suit::HEARTS ||
                          moving_card.suit == cardlib::Suit::DIAMONDS));

  bool lower_rank = static_cast<int>(moving_card.rank) ==
                    static_cast<int>(target_card.rank) - 1;

  return opposite_color && lower_rank;
}

bool SolitaireGame::canMoveToFoundation(const cardlib::Card &card,
                                        int foundation_index) const {
  const auto &pile = foundation_[foundation_index];

  if (pile.empty()) {
    return card.rank == cardlib::Rank::ACE;
  }

  const auto &top_card = pile.back();
  return card.suit == top_card.suit &&
         static_cast<int>(card.rank) == static_cast<int>(top_card.rank) + 1;
}

void SolitaireGame::moveCards(std::vector<cardlib::Card> &from,
                              std::vector<cardlib::Card> &to, size_t count) {
  if (count > from.size())
    return;

  to.insert(to.end(), from.end() - count, from.end());

  from.erase(from.end() - count, from.end());
}

void SolitaireGame::switchGameMode(GameMode mode) {
  if (mode == current_game_mode_)
    return;

  if (win_animation_active_) {
    stopWinAnimation();
  }
    
  // Update the game mode
  current_game_mode_ = mode;

  // Start a new game with the selected mode
  if (mode == GameMode::STANDARD_KLONDIKE) {
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

void SolitaireGame::initializeMultiDeckGame() {
  try {
    // Determine number of decks based on mode
    size_t num_decks = (current_game_mode_ == GameMode::DOUBLE_KLONDIKE) ? 2 : 3;
    
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


void SolitaireGame::dealMultiDeck() {
  // Clear all piles first
  stock_.clear();
  waste_.clear();
  
  // Deal to tableau - i represents the pile number (0-6)
  for (int i = 0; i < 7; i++) {
    // For each pile i, deal i cards face down
    for (int j = 0; j < i; j++) {
      if (auto card = multi_deck_.drawCard()) {
        tableau_[i].emplace_back(*card, false); // face down
        //playSound(GameSoundEvent::CardFlip);
      }
    }
    // Deal one card face up at the end
    if (auto card = multi_deck_.drawCard()) {
      tableau_[i].emplace_back(*card, true); // face up
      //playSound(GameSoundEvent::CardFlip);
    }
  }

  // Move remaining cards to stock (face down)
  while (auto card = multi_deck_.drawCard()) {
    stock_.push_back(*card);
  }

  // Start the deal animation
  startDealAnimation();
}

bool SolitaireGame::checkWinCondition() const {
  // Get the number of decks based on the current mode
  size_t num_decks = (current_game_mode_ == GameMode::STANDARD_KLONDIKE) ? 1 : 
                     (current_game_mode_ == GameMode::DOUBLE_KLONDIKE) ? 2 : 3;
  
  // For multi-deck games, each foundation should have 13 cards
  // There are 4 * num_decks foundations
  for (const auto &pile : foundation_) {
    if (pile.size() != 13)
      return false;
  }

  // Check if all other piles are empty
  return stock_.empty() && waste_.empty() &&
         std::all_of(tableau_.begin(), tableau_.end(),
                    [](const auto &pile) { return pile.empty(); });
}

// Function to refresh the display
void SolitaireGame::refreshDisplay() {
  if (game_area_) {
    gtk_widget_queue_draw(game_area_);
  }
}

int main(int argc, char **argv) {
  SolitaireGame game;
  game.run(argc, argv);
  return 0;
}

void SolitaireGame::initializeCardCache() {
  // Pre-load all card images into cairo surfaces with current dimensions
  cleanupCardCache();

  for (const auto &card : deck_.getAllCards()) {
    if (auto img = deck_.getCardImage(card)) {
      GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
      gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(),
                              nullptr);
      gdk_pixbuf_loader_close(loader, nullptr);

      GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
      GdkPixbuf *scaled =
          gdk_pixbuf_scale_simple(pixbuf, current_card_width_,
                                  current_card_height_, GDK_INTERP_BILINEAR);

      cairo_surface_t *surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
      cairo_t *cr = cairo_create(surface);
      gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);

      std::string key = std::to_string(static_cast<int>(card.suit)) +
                        std::to_string(static_cast<int>(card.rank));
      card_surface_cache_[key] = surface;

      g_object_unref(scaled);
      g_object_unref(loader);
    }
  }

  // Cache card back
  if (auto back_img = deck_.getCardBackImage()) {
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, back_img->data.data(),
                            back_img->data.size(), nullptr);
    gdk_pixbuf_loader_close(loader, nullptr);

    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
        pixbuf, current_card_width_, current_card_height_, GDK_INTERP_BILINEAR);

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
    cairo_t *cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    card_surface_cache_["back"] = surface;

    g_object_unref(scaled);
    g_object_unref(loader);
  }
}

void SolitaireGame::cleanupCardCache() {
  for (auto &[key, surface] : card_surface_cache_) {
    cairo_surface_destroy(surface);
  }
  card_surface_cache_.clear();
}

cairo_surface_t *SolitaireGame::getCardSurface(const cardlib::Card &card) {
  std::string key = std::to_string(static_cast<int>(card.suit)) +
                    std::to_string(static_cast<int>(card.rank));
  auto it = card_surface_cache_.find(key);
  return it != card_surface_cache_.end() ? it->second : nullptr;
}

cairo_surface_t *SolitaireGame::getCardBackSurface() {
  auto it = card_surface_cache_.find("back");
  return it != card_surface_cache_.end() ? it->second : nullptr;
}

void SolitaireGame::setupWindow() {
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

void SolitaireGame::setupGameArea() {
  // Create new drawing area
  game_area_ = gtk_drawing_area_new();
  gtk_box_pack_start(GTK_BOX(vbox_), game_area_, TRUE, TRUE, 0);

  // Enable mouse event handling
  gtk_widget_add_events(
      game_area_,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
          GDK_POINTER_MOTION_MASK |
          GDK_STRUCTURE_MASK); // Enable structure events for resize

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
            SolitaireGame *game = static_cast<SolitaireGame *>(data);
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

  // Initialize the card cache
  initializeCardCache();

  // Make everything visible
  gtk_widget_show_all(window_);
}

void SolitaireGame::setupMenuBar() {
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
                    static_cast<SolitaireGame *>(data)->restartGame();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), restartGameItem);

  // Enter Seed
  GtkWidget *seedItem = gtk_menu_item_new_with_label("Enter Seed...");
  g_signal_connect(G_OBJECT(seedItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<SolitaireGame *>(data)->promptForSeed();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), seedItem);

  // Auto Finish
  GtkWidget *autoFinishItem = gtk_menu_item_new_with_mnemonic("_Auto Finish (F)");
  g_signal_connect(G_OBJECT(autoFinishItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<SolitaireGame *>(data)->autoFinishGame();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), autoFinishItem);

GtkWidget *gameModeItem = gtk_menu_item_new_with_mnemonic("_Game Mode");
GtkWidget *gameModeMenu = gtk_menu_new();
gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameModeItem), gameModeMenu);

// Standard Klondike option (1 deck)
GtkWidget *standardItem = gtk_radio_menu_item_new_with_mnemonic(NULL, "One Deck");
GSList *modeGroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(standardItem));
g_signal_connect(
    G_OBJECT(standardItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<SolitaireGame *>(data)->switchGameMode(SolitaireGame::GameMode::STANDARD_KLONDIKE);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), standardItem);

// Double Klondike option (2 decks)
GtkWidget *doubleItem = gtk_radio_menu_item_new_with_mnemonic(modeGroup, "Two Decks");
g_signal_connect(
    G_OBJECT(doubleItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<SolitaireGame *>(data)->switchGameMode(SolitaireGame::GameMode::DOUBLE_KLONDIKE);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), doubleItem);

// Triple Klondike option (3 decks)
GtkWidget *tripleItem = gtk_radio_menu_item_new_with_mnemonic(modeGroup, "Three Decks");
g_signal_connect(
    G_OBJECT(tripleItem), "activate",
    G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        static_cast<SolitaireGame *>(data)->switchGameMode(SolitaireGame::GameMode::TRIPLE_KLONDIKE);
      }
    }),
    this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameModeMenu), tripleItem);

// Set initial state based on current mode
gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(
        current_game_mode_ == GameMode::STANDARD_KLONDIKE ? standardItem :
        current_game_mode_ == GameMode::DOUBLE_KLONDIKE ? doubleItem : tripleItem),
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

  // ==================== OPTIONS MENU ====================
  GtkWidget *optionsMenu = gtk_menu_new();
  GtkWidget *optionsMenuItem = gtk_menu_item_new_with_mnemonic("_Options");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(optionsMenuItem), optionsMenu);

  // Draw Mode submenu
  GtkWidget *drawModeItem = gtk_menu_item_new_with_mnemonic("_Draw Mode");
  GtkWidget *drawModeMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(drawModeItem), drawModeMenu);

  // Draw One option
  GtkWidget *drawOneItem = gtk_radio_menu_item_new_with_mnemonic(NULL, "_One (1)");
  GSList *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(drawOneItem));
  g_signal_connect(
      G_OBJECT(drawOneItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
          static_cast<SolitaireGame *>(data)->draw_three_mode_ = false;
        }
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(drawModeMenu), drawOneItem);

  // Draw Three option
  GtkWidget *drawThreeItem = gtk_radio_menu_item_new_with_mnemonic(group, "_Three (3)");
  g_signal_connect(
      G_OBJECT(drawThreeItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
          static_cast<SolitaireGame *>(data)->draw_three_mode_ = true;
        }
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(drawModeMenu), drawThreeItem);

  // Set initial state
  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(draw_three_mode_ ? drawThreeItem : drawOneItem),
      TRUE);

  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), drawModeItem);

  // Card Back menu
  GtkWidget *cardBackMenu = gtk_menu_new();
  GtkWidget *cardBackItem = gtk_menu_item_new_with_mnemonic("_Card Back");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(cardBackItem), cardBackMenu);

  // Select custom back option
  GtkWidget *selectBackItem = gtk_menu_item_new_with_mnemonic("_Select Custom Back");
  g_signal_connect(
      G_OBJECT(selectBackItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        SolitaireGame *game = static_cast<SolitaireGame *>(data);

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
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
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
        SolitaireGame *game = static_cast<SolitaireGame *>(data);

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
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
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
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    game->showHowToPlay();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), howToPlayItem);

  // Keyboard Shortcuts
  GtkWidget *shortcutsItem = gtk_menu_item_new_with_mnemonic("_Keyboard Shortcuts");
  g_signal_connect(G_OBJECT(shortcutsItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
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
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    game->dealTestLayout();
                    game->refreshDisplay();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(debugMenu), testLayoutItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), debugMenuItem);
#endif
}

void SolitaireGame::onNewGame(GtkWidget *widget, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
    
  // Check if win animation is active
  if (game->win_animation_active_) {
    game->stopWinAnimation();
  }

  game->current_seed_ = rand();
  game->initializeGame();
  game->updateWindowTitle();
  game->refreshDisplay();
}

void SolitaireGame::restartGame() {
  // Check if win animation is active
  if (win_animation_active_) {
    stopWinAnimation();
  }

  // Keep the current seed and restart the game
  initializeGame();
  refreshDisplay();
}

void SolitaireGame::onQuit(GtkWidget *widget, gpointer data) {
  gtk_main_quit();
}

void SolitaireGame::updateWindowTitle() {
  if (window_) {
    std::string title = "Solitaire - Seed: " + std::to_string(current_seed_);
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
  }
}

void SolitaireGame::onAbout(GtkWidget * /* widget */, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  // Create custom dialog instead of about dialog for more control
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "About Solitaire", GTK_WINDOW(game->window_),
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
      "<span size='x-large' weight='bold'>Solitaire</span>";
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
      "How to Play Solitaire:\n\n"
      "Objective:\n"
      "Build four ordered card piles at the top of the screen, one for each "
      "suit (♣,♦,♥,♠), "
      "starting with Aces and ending with Kings.\n\n"
      "Game Setup:\n"
      "- Seven columns of cards are dealt from left to right\n"
      "- Each column contains one more card than the column to its left\n"
      "- The top card of each column is face up\n"
      "- Remaining cards form the draw pile in the upper left\n\n"
      "Rules:\n"
      "1. In the main playing area, stack cards in descending order (King to "
      "Ace) with alternating colors\n"
      "2. Move single cards or stacks of cards between columns\n"
      "3. When you move a card that was covering a face-down card, the "
      "face-down card is flipped over\n"
      "4. Click the draw pile to reveal new cards when you need them\n"
      "5. Build the four suit piles at the top in ascending order, starting "
      "with Aces\n"
      "6. Empty spaces in the main playing area can only be filled with "
      "Kings\n\n"
      "Controls:\n"
      "- Left-click and drag to move cards\n"
      "- Rigit-click to automatically move cards to the suit piles at the "
      "top\n\n"
      "Keyboard Controls:\n"
      "- Arrow keys (←, →, ↑, ↓) to navigate between piles and cards\n"
      "- Enter to select a card or perform a move\n"
      "- Escape to cancel a selection\n"
      "- Space to draw cards from the stock pile\n"
      "- F to automatically move all possible cards to the foundation piles\n"
      "- 1 or 3 to toggle between Draw One and Draw Three modes\n"
      "- F11 to toggle fullscreen mode\n"
      "- Ctrl+N for a new game\n"
      "- Ctrl+Q to quit\n"
      "- Ctrl+H for help\n\n"
      "Written by Jason Hall\n"
      "Licensed under the MIT License\n"
      "https://github.com/jasonbrianhall/solitaire";

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

void SolitaireGame::dealTestLayout() {
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
  if (current_game_mode_ == GameMode::DOUBLE_KLONDIKE) {
    num_decks = 2;
  } else if (current_game_mode_ == GameMode::TRIPLE_KLONDIKE) {
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

void SolitaireGame::initializeSettingsDir() {
#ifdef _WIN32
    char app_data[MAX_PATH];
    HRESULT hr = SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, app_data);
    if (hr != S_OK) {
        std::cerr << "SHGetFolderPathA failed with code: " << hr << std::endl;
        settings_dir_ = "./";
        return;
    }
    std::cerr << "AppData path: " << app_data << std::endl;
    settings_dir_ = std::string(app_data) + "\\Solitaire";
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
    settings_dir_ = std::string(home) + "/.solitaire";
    mkdir(settings_dir_.c_str(), 0755);
#endif
}

bool SolitaireGame::loadSettings() {
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

void SolitaireGame::saveSettings() {
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

bool SolitaireGame::setCustomCardBack(const std::string &path) {

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

bool SolitaireGame::loadDeck(const std::string &path) {
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

void SolitaireGame::cleanupResources() {
  // Clean up Cairo resources
  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
    buffer_cr_ = nullptr;
  }
  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
    buffer_surface_ = nullptr;
  }

  // Clean up card cache
  cleanupCardCache();
}

void SolitaireGame::resetToDefaultBack() {
  clearCustomBack();
  refreshCardCache();
  refreshDisplay();
}

void SolitaireGame::clearCustomBack() {
  custom_back_path_.clear();

  // Remove the custom back from cache if it exists
  auto it = card_surface_cache_.find("custom_back");
  if (it != card_surface_cache_.end()) {
    cairo_surface_destroy(it->second);
    card_surface_cache_.erase(it);
  }

  // Update settings file
  saveSettings();
}

void SolitaireGame::refreshCardCache() {
  // Clean up existing cache
  cleanupCardCache();

  // Rebuild the cache
  initializeCardCache();

  // If we have a custom back, reload it
  if (!custom_back_path_.empty()) {

    std::ifstream file(custom_back_path_, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      std::vector<char> buffer(size);
      if (file.read(buffer.data(), size)) {
        GError *error = nullptr;
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

        if (gdk_pixbuf_loader_write(loader, (const guchar *)buffer.data(), size,
                                    &error)) {
          gdk_pixbuf_loader_close(loader, &error);

          GdkPixbuf *original_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
          if (original_pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                original_pixbuf, CARD_WIDTH, CARD_HEIGHT, GDK_INTERP_BILINEAR);

            if (scaled) {
              cairo_surface_t *surface = cairo_image_surface_create(
                  CAIRO_FORMAT_ARGB32, CARD_WIDTH, CARD_HEIGHT);
              cairo_t *surface_cr = cairo_create(surface);

              gdk_cairo_set_source_pixbuf(surface_cr, scaled, 0, 0);
              cairo_paint(surface_cr);
              cairo_destroy(surface_cr);

              card_surface_cache_["custom_back"] = surface;

              g_object_unref(scaled);
            }
          }
        }

        if (error) {
          g_error_free(error);
        }
        g_object_unref(loader);
      }
    }
  }
}

void SolitaireGame::onToggleFullscreen(GtkWidget *widget, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->toggleFullscreen();
}

void SolitaireGame::updateCardDimensions(int window_width, int window_height) {
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

double SolitaireGame::getScaleFactor(int window_width, int window_height) const {
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
    case GameMode::DOUBLE_KLONDIKE:
      optimal_width = OPTIMAL_WIDTH_DOUBLE;
      break;
    case GameMode::TRIPLE_KLONDIKE:
      optimal_width = OPTIMAL_WIDTH_TRIPLE;
      break;
    case GameMode::STANDARD_KLONDIKE:
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

void SolitaireGame::autoFinishGame() {
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

void SolitaireGame::processNextAutoFinishMove() {
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
            startFoundationMoveAnimation(top_card, t + 6, pile.size() - 1,
                                         f + 2);

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
      startWinAnimation();
    }
  }
}

gboolean SolitaireGame::onAutoFinishTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->processNextAutoFinishMove();
  return FALSE; // Don't repeat the timer
}

void SolitaireGame::promptForSeed() {
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

void SolitaireGame::showHowToPlay() {
  // Create dialog with OK button
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "How To Play Solitaire", GTK_WINDOW(window_),
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
  const char *title_markup = "<span size='x-large' weight='bold'>How To Play Solitaire</span>";
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

void SolitaireGame::showKeyboardShortcuts() {
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

void SolitaireGame::drawEmptyPile(cairo_t *cr, int x, int y) {
  // Draw a placeholder for an empty pile (cell or foundation)
  cairo_save(cr);
  
  // Draw a rounded rectangle with a thin border
  double radius = 10.0;
  double degrees = G_PI / 180.0;
  
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + current_card_width_ - radius, y + radius, radius, -90 * degrees, 0 * degrees);
  cairo_arc(cr, x + current_card_width_ - radius, y + current_card_height_ - radius, radius, 0 * degrees, 90 * degrees);
  cairo_arc(cr, x + radius, y + current_card_height_ - radius, radius, 90 * degrees, 180 * degrees);
  cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
  cairo_close_path(cr);
  
  // Set a light gray fill with semi-transparency
  cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.5);
  cairo_fill_preserve(cr);
  
  // Set a darker gray border
  cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  
  cairo_restore(cr);
}

void SolitaireGame::showMissingFileDialog(const std::string &filename, 
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

void SolitaireGame::showErrorDialog(const std::string &title, 
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
