#include "spider.h"
#include "spiderdeck.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <array>
#ifdef _WIN32
#include <direct.h>
#include <shlobj.h>
#include <windows.h>
#endif

#ifdef _WIN32
std::string getExecutableDir() { 
    char buffer[MAX_PATH]; 
    GetModuleFileNameA(NULL, buffer, MAX_PATH); 
    std::string path(buffer); size_t pos = path.find_last_of("\\/"); 
    return (pos == std::string::npos) ? "." : path.substr(0, pos); 
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
      sound_enabled_(true),
      rendering_engine_(RenderingEngine::CAIRO),
      opengl_initialized_(false),
      cairo_initialized_(false),
      engine_switch_requested_(false),
      requested_engine_(RenderingEngine::CAIRO),
#ifdef _WIN32
      sounds_zip_path_(getExecutableDir() + "\\sound.zip"),
#else
      sounds_zip_path_("sound.zip"),
#endif
      number_of_suits(1),
      relaxed_rules_mode_(false),
      current_seed_(0) {
  srand(time(NULL));
  current_seed_ = rand();
  initializeGame();
  initializeSettingsDir();
  
  // Load engine preference and initialize rendering
  loadEnginePreference();
  initializeRenderingEngine();
  
  initializeAudio();
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

void SolitaireGame::run(int argc, char **argv) {
  gtk_init(&argc, &argv);
  setupWindow();
  setupGameArea();
  
  // Show all widgets AFTER game area is fully initialized
  // This ensures GL context creation happens when game state is ready
  gtk_widget_show_all(window_);
  
  // NOW switch to OpenGL if that's the configured preference
  // This MUST happen AFTER gtk_widget_show_all() so the GL widget is properly realized
  #ifdef __linux__
  #ifdef USEOPENGL
  if (rendering_engine_ == RenderingEngine::OPENGL) {
    std::cout << "Switching to OpenGL mode after widget realization..." << std::endl;
    gtk_stack_set_visible_child_name(GTK_STACK(rendering_stack_), "opengl");
    // Force processing of pending events to trigger realize callback
    while (gtk_events_pending()) {
      gtk_main_iteration();
    }
  }
  #endif
  #endif
  
  gtk_main();
}

void SolitaireGame::initializeGame() {
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
      throw std::runtime_error("Could not find cards.zip in any search path");
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
    exit(1);
  }
}

// ============================================================================
// RENDERING ENGINE IMPLEMENTATION
// ============================================================================

bool SolitaireGame::isOpenGLSupported() const {
  #ifndef USEOPENGL
  return false;
  #else
  return true;
  #endif
}

bool SolitaireGame::setRenderingEngine(RenderingEngine engine) {
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

bool SolitaireGame::initializeRenderingEngine() {
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

bool SolitaireGame::switchRenderingEngine(RenderingEngine newEngine) {
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
  
  cache_dirty_ = true;
  
  rendering_engine_ = newEngine;
  
  if (newEngine == RenderingEngine::OPENGL) {
    cairo_initialized_ = false;
    opengl_initialized_ = true;
  } else {
    opengl_initialized_ = false;
    cairo_initialized_ = true;
  }
  
  const char *view = (newEngine == RenderingEngine::OPENGL) ? "opengl" : "cairo";
  gtk_stack_set_visible_child_name(GTK_STACK(rendering_stack_), view);
  
  rendering_engine_ = newEngine;
  std::cout << "Switched to " << getRenderingEngineName() << std::endl;
  saveEnginePreference();
  return true;
  #else
  return false;
  #endif
}

void SolitaireGame::cleanupRenderingEngine() {
  if (rendering_engine_ == RenderingEngine::OPENGL) {
    #ifdef USEOPENGL
    cleanupOpenGLResources_gl();
    #endif
  } else {
    cleanupCardCache();
  }
}

std::string SolitaireGame::getRenderingEngineName() const {
  switch (rendering_engine_) {
    case RenderingEngine::CAIRO:
      return "Cairo";
    case RenderingEngine::OPENGL:
      return "OpenGL";
    default:
      return "Unknown";
  }
}

void SolitaireGame::renderFrame() {
  switch (rendering_engine_) {
    case RenderingEngine::CAIRO:
      refreshDisplay();
      break;
    case RenderingEngine::OPENGL:
      #ifdef USEOPENGL
      renderFrame_gl();
      #endif
      break;
    default:
      refreshDisplay();
  }
}

void SolitaireGame::saveEnginePreference() {
  std::string config_file = settings_dir_ + "/graphics.ini";
  std::ofstream config(config_file);
  if (config.is_open()) {
    config << "[Graphics]\nengine=" 
           << (rendering_engine_ == RenderingEngine::CAIRO ? "cairo" : "opengl") << "\n";
    config.close();
  }
}

void SolitaireGame::loadEnginePreference() {
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

void SolitaireGame::addEngineSelectionMenu(GtkWidget *menubar) {
  GtkWidget *graphics_menu = gtk_menu_new();
  GtkWidget *graphics_item = gtk_menu_item_new_with_label("Graphics");

  GtkWidget *cairo_item = gtk_menu_item_new_with_label("Use Cairo (CPU)");
  g_signal_connect(cairo_item, "activate",
                   G_CALLBACK(+[](GtkWidget *w, gpointer data) {
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
                     game->switchRenderingEngine(RenderingEngine::CAIRO);
                     game->refreshDisplay();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(graphics_menu), cairo_item);

  #ifdef __linux__
  GtkWidget *opengl_item = gtk_menu_item_new_with_label("Use OpenGL (GPU)");
  g_signal_connect(opengl_item, "activate",
                   G_CALLBACK(+[](GtkWidget *w, gpointer data) {
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
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

bool SolitaireGame::isValidDragSource(int pile_index, int card_index) const {
  if (pile_index < 0)
    return false;

  // Stock pile - not draggable
  if (pile_index == 0) {
    return false;
  }
  
  // Waste pile - not used in Spider Solitaire
  if (pile_index == 1) {
    return false;
  }

  // Foundation pile - not draggable in Spider
  if (pile_index >= 2 && pile_index <= 5) {
    return false;
  }

  // Tableau piles (6-15 for Spider with 10 piles)
  if (pile_index >= 6 && pile_index <= 15) {
    const auto &pile = tableau_[pile_index - 6];
    
    // Must be a valid index and the card must be face up
    if (pile.empty() || card_index < 0 || static_cast<size_t>(card_index) >= pile.size() || !pile[card_index].face_up) {
      return false;
    }
    
    // Check if cards from card_index to the end form a valid sequence
    // For Spider, a valid sequence requires:
    // 1. Always: consecutive descending ranks (King, Queen, Jack, etc.)
    // 2. In order to move multiple cards: they must all be the same suit
    
    // Single card is always valid to drag
    if (card_index == static_cast<int>(pile.size()) - 1) {
      return true;
    }
    
    // Check if all cards from card_index to end are the same suit
    bool all_same_suit = true;
    cardlib::Suit first_suit = pile[card_index].card.suit;
    
    // First check if we have consecutive descending ranks
    for (size_t i = card_index; i < pile.size() - 1; i++) {
      // Check consecutive rank
      bool consecutive_rank = static_cast<int>(pile[i].card.rank) == 
                             static_cast<int>(pile[i+1].card.rank) + 1;
      
      if (!consecutive_rank) {
        return false; // Can't drag non-consecutive cards
      }
      
      // Track if all cards are same suit
      if (pile[i+1].card.suit != first_suit) {
        all_same_suit = false;
      }
    }
    
    // In Spider, when dragging multiple cards, they must be in consecutive
    // descending rank AND the same suit
    return all_same_suit;
  }

  return false;
}

std::vector<cardlib::Card> &SolitaireGame::getPileReference(int pile_index) {
  if (pile_index == 0)
    return stock_;
  if (pile_index == 1)
    return waste_;
  if (pile_index >= 2 && pile_index <= 5)
    return foundation_[pile_index - 2];
  if (pile_index >= 6 && pile_index <= 12) {
    // We need to handle tableau differently or change the function signature
    throw std::runtime_error(
        "Cannot get reference to tableau pile - type mismatch");
  }
  throw std::out_of_range("Invalid pile index");
}

void SolitaireGame::deal() {
  // Determine number of decks based on difficulty
  int num_suits = number_of_suits;  // Default to single suit (difficult)
  
  // Clear all piles
  stock_.clear();
  tableau_.clear();
  foundation_.clear(); 

  // Initialize foundation - we only need one pile in Spider to track completed sequences
  foundation_.resize(number_of_suits);

  // Create the appropriate Spider Deck
  cardlib::SpiderDeck spider_deck(num_suits);
  
  // Shuffle the decks
  spider_deck.shuffle(current_seed_);

  // Resize tableau to 10 piles
  tableau_.resize(10);

  // Deal initial tableau
  // First 4 piles get 6 cards each
  // Last 6 piles get 5 cards each
  for (int i = 0; i < 10; i++) {
    int cards_to_deal = (i < 6) ? 6 : 5;
    
    for (int j = 0; j < cards_to_deal; j++) {
      if (auto card = spider_deck.drawCard()) {
        // Only the top card in each pile is face up
        tableau_[i].emplace_back(*card, j == cards_to_deal - 1);
        playSound(GameSoundEvent::CardFlip);
      }
    }
  }

  // Remaining cards go to stock
  while (auto card = spider_deck.drawCard()) {
    stock_.push_back(*card);
  }

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

std::vector<cardlib::Card> SolitaireGame::getDragCards(int pile_index,
                                                      int card_index) {
  std::vector<cardlib::Card> result;
  
  // Only handle tableau piles for Spider Solitaire
  if (pile_index >= 6 && pile_index <= 15) {
    const auto &tableau_pile = tableau_[pile_index - 6];
    if (card_index >= 0 &&
        static_cast<size_t>(card_index) < tableau_pile.size() &&
        tableau_pile[card_index].face_up) {
      
      // For Spider, we can drag:
      // 1. A single card
      // 2. A sequence of descending cards of the same suit
      
      // Start with the selected card
      result.push_back(tableau_pile[card_index].card);
      
      // If it's a single card, we're done
      if (card_index == static_cast<int>(tableau_pile.size()) - 1) {
        return result;
      }
      
      // For multiple cards, check if they form a valid same-suit sequence
      cardlib::Suit first_suit = tableau_pile[card_index].card.suit;
      cardlib::Rank current_rank = tableau_pile[card_index].card.rank;
      bool valid_sequence = true;
      
      // Check each card after the selected one
      for (size_t i = card_index + 1; i < tableau_pile.size(); i++) {
        const cardlib::Card &next_card = tableau_pile[i].card;
        
        // Must be consecutive descending rank AND same suit
        if (static_cast<int>(next_card.rank) != static_cast<int>(current_rank) - 1 ||
            next_card.suit != first_suit) {
          valid_sequence = false;
          break;
        }
        
        result.push_back(next_card);
        current_rank = next_card.rank;
      }
      
      // If not a valid sequence, just return the single selected card
      if (!valid_sequence) {
        result.clear();
        result.push_back(tableau_pile[card_index].card);
      }
    }
  }
  
  return result;
}

gboolean SolitaireGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                      gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (game->auto_finish_active_ || game->sequence_animation_active_) {
      return TRUE;
  }


  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

if (game->win_animation_active_) {
  game->stopWinAnimation();
  return TRUE;
}

  // If any animation is active, block all interactions
  if (game->foundation_move_animation_active_ ||
      game->stock_to_waste_animation_active_) {
    return TRUE;
  }

  if (event->button == 1) { // Left click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    if (pile_index == 0) { // Stock pile
      game->handleStockPileClick();
      return TRUE;
    }

    if (pile_index >= 0 && game->isValidDragSource(pile_index, card_index)) {
      game->dragging_ = true;
      game->drag_source_pile_ = pile_index;
      game->drag_start_x_ = event->x;
      game->drag_start_y_ = event->y;
      game->drag_cards_ = game->getDragCards(pile_index, card_index);
      game->playSound(GameSoundEvent::CardFlip);
      // Calculate offsets
      int x_offset_multiplier;
      if (pile_index >= 6) {
        x_offset_multiplier = pile_index - 6;
      } else if (pile_index >= 2 && pile_index <= 5) {
        x_offset_multiplier = pile_index + 1;
      } else if (pile_index == 1) {
        x_offset_multiplier = 1;
      } else {
        x_offset_multiplier = 0;
      }

      game->drag_offset_x_ =
          event->x - (game->current_card_spacing_ +
                      x_offset_multiplier * (game->current_card_width_ +
                                             game->current_card_spacing_));

      if (pile_index >= 6) {
        game->drag_offset_y_ =
            event->y -
            (game->current_card_spacing_ + game->current_card_height_ +
             game->current_vert_spacing_ +
             card_index * game->current_vert_spacing_);
      } else {
        game->drag_offset_y_ = event->y - game->current_card_spacing_;
      }
    }
  } else if (event->button == 3) { // Right click
    return TRUE;
  }

  return TRUE;
}

gboolean SolitaireGame::onButtonRelease(GtkWidget *widget,
                                        GdkEventButton *event, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->keyboard_navigation_active_ = false;

  if (event->button == 1 && game->dragging_) {
    auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

    if (target_pile >= 6 && target_pile <= 15) { // Tableau piles
      auto &source_tableau = game->tableau_[game->drag_source_pile_ - 6];
      auto &target_tableau = game->tableau_[target_pile - 6];
      
      std::vector<cardlib::Card> target_cards;
      if (!target_tableau.empty()) {
        target_cards = {target_tableau.back().card};
      }

      if (game->canMoveToPile(game->drag_cards_, target_cards, false)) {
        // Remove cards from source pile
        source_tableau.erase(source_tableau.end() - game->drag_cards_.size(), source_tableau.end());

        // Flip over the new top card in source pile if necessary
        if (!source_tableau.empty() && !source_tableau.back().face_up) {
          source_tableau.back().face_up = true;
          game->playSound(GameSoundEvent::CardFlip);
        }

        // Add cards to target tableau
        for (const auto &card : game->drag_cards_) {
          target_tableau.emplace_back(card, true);
        }
        
        game->playSound(GameSoundEvent::CardPlace);
        
        // Check if the move created a completed sequence
        game->checkForCompletedSequence(target_pile - 6);
        
        // Check if the player has won
        if (game->checkWinCondition()) {
          game->startWinAnimation();
        }
      }
    }

    game->dragging_ = false;
    game->drag_cards_.clear();
    game->drag_source_pile_ = -1;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}

void SolitaireGame::handleStockPileClick() {
  if (stock_.empty()) {
    // No more cards in stock pile
    return;
  }

  // Check if all piles have at least one card - required for Spider Solitaire
  bool can_deal = true;
  
  // Only perform the check if we're using standard rules
  if (!relaxed_rules_mode_) {
    for (const auto& pile : tableau_) {
      if (pile.empty()) {
        can_deal = false;
        break;
      }
    }
  }

  if (!can_deal) {
    // Show message that all tableau piles must have at least one card
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, 
        "All tableau piles must have at least one card before dealing more cards.\n"
        "You can enable relaxed rules from the Game menu if you prefer.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }

  // Deal remaining cards, up to one per tableau pile
  if (!stock_to_waste_animation_active_) {
    // Calculate how many piles we can deal to
    int piles_to_deal = std::min(static_cast<int>(tableau_.size()), static_cast<int>(stock_.size()));
    
    // Deal one card to each tableau pile, up to the number of cards left
    for (int i = 0; i < piles_to_deal; i++) {
      if (!stock_.empty()) {
        // Get a card from stock
        cardlib::Card card = stock_.back();
        stock_.pop_back();
        
        // Add to tableau pile face up
        tableau_[i].emplace_back(card, true);
        
        // Play card dealing sound
        playSound(GameSoundEvent::DealCard);
      }
    }
    
    // Debug output to verify stock pile is empty when expected
    #ifdef DEBUG
    std::cout << "After dealing, stock pile size: " << stock_.size() << std::endl;
    #endif
    
    refreshDisplay();
  }
}

bool SolitaireGame::tryMoveToFoundation(const cardlib::Card &card) {
  // Try each foundation pile
  for (size_t i = 0; i < foundation_.size(); i++) {
    std::vector<cardlib::Card> cards = {card};
    if (canMoveToPile(cards, foundation_[i], true)) {
      foundation_[i].push_back(card);
      return true;
    }
  }
  return false;
}

gboolean SolitaireGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                       gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (game->dragging_) {
    game->drag_start_x_ = event->x;
    game->drag_start_y_ = event->y;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}

std::pair<int, int> SolitaireGame::getPileAt(int x, int y) const {
  // Check stock pile
  if (x >= current_card_spacing_ &&
      x <= current_card_spacing_ + current_card_width_ &&
      y >= current_card_spacing_ &&
      y <= current_card_spacing_ + current_card_height_) {
    return {0, stock_.empty() ? -1 : 0};
  }

  // Foundation piles are not used in the same way in Spider
  // We'll just keep one pile for counting completed sequences
  int foundation_x = 3 * (current_card_width_ + current_card_spacing_);
  for (int i = 0; i < 4; i++) {
    if (x >= foundation_x && x <= foundation_x + current_card_width_ &&
        y >= current_card_spacing_ &&
        y <= current_card_spacing_ + current_card_height_) {
      return {2 + i, foundation_[i].empty()
                         ? -1
                         : static_cast<int>(foundation_[i].size() - 1)};
    }
    foundation_x += current_card_width_ + current_card_spacing_;
  }

  // Check tableau piles - check from top card down
  // Spider has 10 tableau piles
  int tableau_y =
      current_card_spacing_ + current_card_height_ + current_vert_spacing_;
  
  // Calculate the width available for tableau piles
  int available_width = current_card_width_ * tableau_.size() + 
                       current_card_spacing_ * (tableau_.size() + 1);
  
  // Calculate starting x position to center the tableau piles
  int start_x = current_card_spacing_;
  
  for (int i = 0; i < tableau_.size(); i++) {
    int pile_x = start_x + i * (current_card_width_ + current_card_spacing_);
    if (x >= pile_x && x <= pile_x + current_card_width_) {
      const auto &pile = tableau_[i];
      if (pile.empty() && y >= tableau_y &&
          y <= tableau_y + current_card_height_) {
        return {6 + i, -1};
      }

      // Check cards from top to bottom
      for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
        int card_y = tableau_y + j * current_vert_spacing_;
        if (y >= card_y && y <= card_y + current_card_height_) {
          if (pile[j].face_up) {
            return {6 + i, j};
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

  const auto &moving_card = cards[0]; // First card in the sequence being moved
  
  // Foundation pile rules (keep for compatibility)
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

  // Tableau pile rules for Spider Solitaire
  if (target.empty()) {
    // Any card can go to an empty tableau pile in Spider
    return true;
  }

  const auto &target_card = target.back();

  // In Spider, any card can be placed on another card one rank higher
  // (regardless of suit)
  bool lower_rank = static_cast<int>(moving_card.rank) ==
                   static_cast<int>(target_card.rank) - 1;

  return lower_rank;
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

bool SolitaireGame::checkWinCondition() const {
  // For Spider Solitaire, we win when:
  // 1. We have 8 completed sequences (each sequence is K through A of one suit)
  // 2. There are no more cards left in stock or tableau piles
  
  // Check if we have the required number of completed sequences
  int completed_sequences = foundation_[0].size();
  
  // For single-suit Spider, we need 8 completed sequences
  if (completed_sequences < 8) {
    return false;
  }
  
  // All sequences are completed, now check if we have any cards left
  // We already know all 104 cards in the Spider deck are accounted for
  // (8 sequences of 13 cards = 104 cards), so we should win automatically
  // after the 8th sequence is completed
  return true;
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

void SolitaireGame::cleanupCardCache() {
  for (auto &[key, surface] : card_surface_cache_) {
    cairo_surface_destroy(surface);
  }
  card_surface_cache_.clear();
}

void SolitaireGame::setupWindow() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Spider Solitaire");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit),
                   NULL);

  gtk_widget_add_events(window_, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window_), "key-press-event", G_CALLBACK(onKeyPress),
                   this);

  // Make sure the window is realized before calculating scale
  gtk_widget_realize(window_);
    
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

void SolitaireGame::setupCairoArea() {
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
  
  // CREATE CAIRO BUFFER - CRITICAL!
  buffer_surface_ = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
  buffer_cr_ = cairo_create(buffer_surface_);

  // Initialize the card cache
  initializeCardCache();
}

#ifdef USEOPENGL
void SolitaireGame::setupOpenGLArea() {
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
        SolitaireGame *game = static_cast<SolitaireGame *>(data);
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

void SolitaireGame::setupGameArea() {
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

void SolitaireGame::setupMenuBar() {
  GtkWidget *menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(vbox_), menubar, FALSE, FALSE, 0);

  //=======================================================
  // GAME MENU - Core game functions
  //=======================================================
  GtkWidget *gameMenu = gtk_menu_new();
  GtkWidget *gameMenuItem = gtk_menu_item_new_with_mnemonic("_Game");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

  // Game control section
  GtkWidget *newGameItem = gtk_menu_item_new_with_mnemonic("_New Game");
  g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

  GtkWidget *restartGameItem = gtk_menu_item_new_with_label("Restart Current Game");
  g_signal_connect(G_OBJECT(restartGameItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<SolitaireGame *>(data)->restartGame();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), restartGameItem);

  // Auto-finish option
  GtkWidget *autoFinishItem = gtk_menu_item_new_with_mnemonic("Auto-_Finish Game");
  g_signal_connect(G_OBJECT(autoFinishItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<SolitaireGame *>(data)->autoFinishGame();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), autoFinishItem);
  
  // Separator before difficulty options
  GtkWidget *sep1 = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep1);

  // Difficulty submenu
  GtkWidget *difficultyItem = gtk_menu_item_new_with_mnemonic("_Difficulty");
  GtkWidget *difficultyMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(difficultyItem), difficultyMenu);

  // Create radio menu items for difficulty levels
  GSList *group = NULL;

  // Easy option (1 suit)
  GtkWidget *easyItem = gtk_radio_menu_item_new_with_mnemonic(group, "Easy (1 Suit)");
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(easyItem));
  
  // Medium option (2 suits)
  GtkWidget *mediumItem = gtk_radio_menu_item_new_with_mnemonic(group, "Medium (2 Suits)");
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mediumItem));
  
  // Hard option (4 suits)
  GtkWidget *hardItem = gtk_radio_menu_item_new_with_mnemonic(group, "Hard (4 Suits)");
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(hardItem));

  // Set the active radio button based on current difficulty
  if (number_of_suits == 1) {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(easyItem), TRUE);
  } else if (number_of_suits == 2) {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mediumItem), TRUE);
  } else if (number_of_suits == 4) {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(hardItem), TRUE);
  }

  // Connect signals for the difficulty options
  g_signal_connect(G_OBJECT(easyItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
                      if (game->number_of_suits != 1) {
                        game->number_of_suits = 1;
                        game->promptForNewGame("Easy");
                      }
                    }
                  }),
                  this);

  g_signal_connect(G_OBJECT(mediumItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
                      if (game->number_of_suits != 2) {
                        game->number_of_suits = 2;
                        game->promptForNewGame("Medium");
                      }
                    }
                  }),
                  this);

  g_signal_connect(G_OBJECT(hardItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
                      if (game->number_of_suits != 4) {
                        game->number_of_suits = 4;
                        game->promptForNewGame("Hard");
                      }
                    }
                  }),
                  this);

  // Add radio buttons to difficulty menu
  gtk_menu_shell_append(GTK_MENU_SHELL(difficultyMenu), easyItem);
  gtk_menu_shell_append(GTK_MENU_SHELL(difficultyMenu), mediumItem);
  gtk_menu_shell_append(GTK_MENU_SHELL(difficultyMenu), hardItem);

  // Add difficulty menu to game menu
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), difficultyItem);
  
  // Game Rules option
  GtkWidget *relaxedRulesItem = gtk_check_menu_item_new_with_mnemonic("_Relaxed Rules");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(relaxedRulesItem), relaxed_rules_mode_);
  g_signal_connect(G_OBJECT(relaxedRulesItem), "toggled",
                 G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                   SolitaireGame *game = static_cast<SolitaireGame *>(data);
                   game->relaxed_rules_mode_ = gtk_check_menu_item_get_active(
                       GTK_CHECK_MENU_ITEM(widget));
                 }),
                 this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), relaxedRulesItem);

  // Add tooltip for relaxed rules
  gtk_widget_set_tooltip_text(relaxedRulesItem, 
                         "When enabled, allows dealing cards from the stock pile "
                         "even when some tableau piles are empty");
  
  // Separator before advanced options
  GtkWidget *sep2 = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep2);

  // Advanced options submenu
  GtkWidget *advancedItem = gtk_menu_item_new_with_mnemonic("_Advanced Options");
  GtkWidget *advancedMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(advancedItem), advancedMenu);

  // Custom seed option
  GtkWidget *seedItem = gtk_menu_item_new_with_label("Set Custom Seed...");
  g_signal_connect(G_OBJECT(seedItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<SolitaireGame *>(data)->promptForSeed();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(advancedMenu), seedItem);

#ifdef DEBUG
  // Test layout option (debug only)
  GtkWidget *testLayoutItem = gtk_menu_item_new_with_label("Test Layout");
  g_signal_connect(G_OBJECT(testLayoutItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    game->dealTestLayout();
                    game->refreshDisplay();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(advancedMenu), testLayoutItem);
#endif

  // Add advanced submenu to game menu
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), advancedItem);

  // Separator before quit
  GtkWidget *sep3 = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep3);

  // Quit option
  GtkWidget *quitItem = gtk_menu_item_new_with_mnemonic("_Quit");
  g_signal_connect(G_OBJECT(quitItem), "activate", G_CALLBACK(onQuit), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), quitItem);

  // Add Game menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), gameMenuItem);

  //=======================================================
  // OPTIONS MENU - Visual, audio, and appearance options
  //=======================================================
  GtkWidget *optionsMenu = gtk_menu_new();
  GtkWidget *optionsMenuItem = gtk_menu_item_new_with_mnemonic("_Options");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(optionsMenuItem), optionsMenu);

  // Sound option
  GtkWidget *soundItem = gtk_check_menu_item_new_with_mnemonic("Enable _Sound");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(soundItem), sound_enabled_);
  g_signal_connect(G_OBJECT(soundItem), "toggled",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    game->sound_enabled_ = gtk_check_menu_item_get_active(
                        GTK_CHECK_MENU_ITEM(widget));
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), soundItem);

  // Fullscreen toggle
  GtkWidget *fullscreenItem = gtk_menu_item_new_with_mnemonic("Toggle _Fullscreen");
  g_signal_connect(G_OBJECT(fullscreenItem), "activate", G_CALLBACK(onToggleFullscreen), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), fullscreenItem);

  // Separator before appearance options
  GtkWidget *sepOptions = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), sepOptions);

  //=========== Card appearance options ============

  // Card appearance submenu
  GtkWidget *cardAppearanceItem = gtk_menu_item_new_with_mnemonic("Card _Appearance");
  GtkWidget *cardAppearanceMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(cardAppearanceItem), cardAppearanceMenu);

  // Custom card back options
  GtkWidget *selectBackItem = gtk_menu_item_new_with_mnemonic("Select Custom Card _Back...");
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
  gtk_menu_shell_append(GTK_MENU_SHELL(cardAppearanceMenu), selectBackItem);

  GtkWidget *resetBackItem = gtk_menu_item_new_with_mnemonic("_Reset to Default Card Back");
  g_signal_connect(G_OBJECT(resetBackItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    game->resetToDefaultBack();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(cardAppearanceMenu), resetBackItem);

  // Load custom deck option
  GtkWidget *loadDeckItem = gtk_menu_item_new_with_mnemonic("Load Custom Card _Deck...");
  g_signal_connect(
      G_OBJECT(loadDeckItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        SolitaireGame *game = static_cast<SolitaireGame *>(data);

        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Load Card Deck", GTK_WINDOW(game->window_),
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
  gtk_menu_shell_append(GTK_MENU_SHELL(cardAppearanceMenu), loadDeckItem);

  // Add card appearance submenu to options menu
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), cardAppearanceItem);

  // Add Options menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), optionsMenuItem);

  //=======================================================
  // HELP MENU
  //=======================================================
  GtkWidget *helpMenu = gtk_menu_new();
  GtkWidget *helpMenuItem = gtk_menu_item_new_with_mnemonic("_Help");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMenuItem), helpMenu);

  // How to Play item
  GtkWidget *howToPlayItem = gtk_menu_item_new_with_mnemonic("_How to Play");
  g_signal_connect(G_OBJECT(howToPlayItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    
                    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                        "How to Play Spider Solitaire", GTK_WINDOW(game->window_),
                        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                                   GTK_DIALOG_DESTROY_WITH_PARENT),
                        "OK", GTK_RESPONSE_OK, NULL);
                    
                    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);
                    
                    GtkWidget *instructions_text = gtk_text_view_new();
                    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(instructions_text), GTK_WRAP_WORD);
                    gtk_text_view_set_editable(GTK_TEXT_VIEW(instructions_text), FALSE);
                    
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(instructions_text));
                    const char *instructions =
                      "How to Play Spider Solitaire:\n\n"
                      "Objective:\n"
                      "Create 8 complete sequences of cards from King down to Ace, all of the same suit. "
                      "When a sequence is complete, it is automatically removed from the table.\n\n"
                      "Game Setup:\n"
                      "- The game is played with 104 cards (2 decks)\n"
                      "- Cards are dealt into 10 tableau columns\n"
                      "- First 4 columns receive 6 cards each, last 6 columns receive 5 cards each\n"
                      "- Only the top card of each column is face up initially\n"
                      "- Remaining cards form the stock pile at the bottom of the screen\n\n"
                      "Rules:\n"
                      "1. You can move cards from one tableau column to another if they follow a descending sequence "
                      "(regardless of suit)\n"
                      "2. To move a group of cards together, they must be in descending sequence AND of the same suit\n"
                      "3. Empty tableau spaces can be filled with any card or valid sequence\n"
                      "4. You can deal a new row of cards (one to each tableau column) by clicking the stock pile\n"
                      "5. You can only deal from the stock pile when all tableau columns have at least one card\n"
                      "6. When a sequence from King to Ace of the same suit is formed, it is automatically removed\n"
                      "7. The game is won when all 8 same-suit sequences have been completed";
                    
                    gtk_text_buffer_set_text(buffer, instructions, -1);
                    
                    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
                    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
                    gtk_widget_set_size_request(scrolled_window, 450, 350);
                    
                    gtk_container_add(GTK_CONTAINER(scrolled_window), instructions_text);
                    gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);
                    
                    gtk_widget_show_all(dialog);
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), howToPlayItem);

  // Keyboard shortcuts item
  GtkWidget *shortcutsItem = gtk_menu_item_new_with_mnemonic("_Keyboard Shortcuts");
  g_signal_connect(G_OBJECT(shortcutsItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    SolitaireGame *game = static_cast<SolitaireGame *>(data);
                    
                    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                        "Keyboard Shortcuts", GTK_WINDOW(game->window_),
                        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                                   GTK_DIALOG_DESTROY_WITH_PARENT),
                        "OK", GTK_RESPONSE_OK, NULL);
                    
                    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);
                    
                    GtkWidget *label = gtk_label_new(NULL);
                    const char *markup = 
                        "<span size='large' weight='bold'>Keyboard Shortcuts</span>\n\n"
                        "<b>F11</b> - Toggle Fullscreen\n"
                        "<b>Ctrl+N</b> - New Game\n"
                        "<b>Ctrl+Q</b> - Quit\n"
                        "<b>Ctrl+H</b> - Help\n"
                        "<b>Arrow Keys</b> - Navigate piles\n"
                        "<b>Enter</b> - Select or place cards\n"
                        "<b>Esc</b> - Cancel selection\n"
                        "<b>Space</b> - Deal cards from stock pile\n"
                        "<b>F</b> - Auto-Finish (find best moves)";
                    
                    gtk_label_set_markup(GTK_LABEL(label), markup);
                    gtk_container_add(GTK_CONTAINER(content_area), label);
                    gtk_widget_show_all(dialog);
                    
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), shortcutsItem);

  // About item
  GtkWidget *aboutItem = gtk_menu_item_new_with_mnemonic("_About");
  g_signal_connect(G_OBJECT(aboutItem), "activate", G_CALLBACK(onAbout), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutItem);

  // Add Graphics menu for rendering engine selection (before Help menu)
  addEngineSelectionMenu(menubar);

  // Add Help menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMenuItem);

  // Show all menu items
  gtk_widget_show_all(menubar);
}

void SolitaireGame::onNewGame(GtkWidget *widget, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
    
  // Check if win animation is active
  if (game->win_animation_active_) {
    game->stopWinAnimation();
  }

  game->current_seed_ = rand();

  game->initializeGame();
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

void SolitaireGame::onAbout(GtkWidget * /* widget */, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  // Create custom dialog instead of about dialog for more control
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "About Spider Solitaire", GTK_WINDOW(game->window_),
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
      "<span size='x-large' weight='bold'>Spider Solitaire</span>";
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
      "How to Play Spider Solitaire:\n\n"
      "Objective:\n"
      "Create 8 complete sequences of cards from King down to Ace, all of the same suit. "
      "When a sequence is complete, it is automatically removed from the table.\n\n"
      "Game Setup:\n"
      "- The game is played with 104 cards (2 decks)\n"
      "- Cards are dealt into 10 tableau columns\n"
      "- First 4 columns receive 6 cards each, last 6 columns receive 5 cards each\n"
      "- Only the top card of each column is face up initially\n"
      "- Remaining cards form the stock pile at the bottom of the screen\n\n"
      "Rules:\n"
      "1. You can move cards from one tableau column to another if they follow a descending sequence "
      "(regardless of suit)\n"
      "2. To move a group of cards together, they must be in descending sequence AND of the same suit\n"
      "3. Empty tableau spaces can be filled with any card or valid sequence\n"
      "4. You can deal a new row of cards (one to each tableau column) by clicking the stock pile\n"
      "5. You can only deal from the stock pile when all tableau columns have at least one card\n"
      "6. When a sequence from King to Ace of the same suit is formed, it is automatically removed\n"
      "7. The game is won when all 8 same-suit sequences have been completed\n\n"
      "Difficulty Levels:\n"
      "- Easy: 1 suit (all Spades)\n"
      "- Medium: 2 suits (Spades and Hearts)\n"
      "- Hard: 4 suits (standard deck)\n\n"
      "Controls:\n"
      "- Left-click and drag to move cards\n"
      "- Click on the stock pile to deal more cards\n\n"
      "Keyboard Controls:\n"
      "- Arrow keys (←, →, ↑, ↓) to navigate between piles and cards\n"
      "- Enter to select a card or perform a move\n"
      "- Escape to cancel a selection\n"
      "- F to auto-finish any valid moves (automatically finds best moves)\n"
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
  stock_.clear();
  waste_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset foundation and tableau
  foundation_.resize(1);
  tableau_.resize(10);

  // Create a test layout that's almost solved with all four suits
  std::array<cardlib::Suit, 4> suits = {
    cardlib::Suit::SPADES,
    cardlib::Suit::HEARTS,
    cardlib::Suit::DIAMONDS,
    cardlib::Suit::CLUBS
  };
  
  // Create 7 nearly completed sequences (K-2) that just need to be connected to Aces
  // We'll distribute these among the 4 suits (2 sequences per suit, except 1 for one suit)
  int suitIndex = 0;
  for (int i = 0; i < 7; i++) {
    cardlib::Suit currentSuit = suits[suitIndex];
    
    // Put a sequence from King down to 2 in tableau piles 0-6
    for (int rank = static_cast<int>(cardlib::Rank::KING); 
         rank >= static_cast<int>(cardlib::Rank::TWO); rank--) {
      // Create a Card object first, then use it to create a TableauCard
      cardlib::Card card(currentSuit, static_cast<cardlib::Rank>(rank));
      tableau_[i].emplace_back(card, true); // Set face-up to true
    }
    
    // Cycle through suits
    suitIndex = (suitIndex + 1) % 4;
  }

  // Put the Aces in convenient locations to complete the sequences
  // We'll distribute 8 aces among remaining piles 7-9
  
  // Calculate how many of each suit we need based on the sequences we created
  std::array<int, 4> acesNeeded = {0, 0, 0, 0};
  suitIndex = 0;
  for (int i = 0; i < 7; i++) {
    acesNeeded[suitIndex]++;
    suitIndex = (suitIndex + 1) % 4;
  }
  
  // Add final needed ace for the 8th sequence
  acesNeeded[0]++;
  
  // Place aces on piles 7-9
  // Two aces in pile 7, three in pile 8, three in pile 9
  for (int suit = 0; suit < 4; suit++) {
    for (int i = 0; i < acesNeeded[suit]; i++) {
      cardlib::Card ace(suits[suit], cardlib::Rank::ACE);
      
      // Distribute aces across piles 7-9
      if (tableau_[7].size() < 2) {
        tableau_[7].emplace_back(ace, true);
      } else if (tableau_[8].size() < 3) {
        tableau_[8].emplace_back(ace, true);
      } else {
        tableau_[9].emplace_back(ace, true);
      }
    }
  }
  
  // For the 8th sequence, add a complete K-2 sequence of the first suit to pile 0
  // (We already added its matching ace above)
  for (int rank = static_cast<int>(cardlib::Rank::KING); 
       rank >= static_cast<int>(cardlib::Rank::TWO); rank--) {
    cardlib::Card card(suits[0], static_cast<cardlib::Rank>(rank));
    tableau_[0].emplace_back(card, true);
  }
  
  // This is a trivially solvable layout - just move each Ace to the end of the corresponding
  // K-2 sequence with the same suit, and each completed sequence will move to the foundation
  playSound(GameSoundEvent::CardFlip);
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
    if (line.length() >= 10 && line.substr(0, 10) == "card_back=") {  // Check length first!
      custom_back_path_ = line.substr(10);
      std::cerr << "Loaded custom back path: " << custom_back_path_ << std::endl;
    }
  }

  return true;
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

void SolitaireGame::onToggleFullscreen(GtkWidget *widget, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->toggleFullscreen();
}

void SolitaireGame::updateCardDimensions(int window_width, int window_height) {
  // Calculate optimal card width based on window width
  // Use a more aggressive approach to fill screen space
  // Allow for minimal spacing between cards
  int min_spacing = 10; // Minimum spacing between cards
  int total_spacing = (tableau_.size() + 1) * min_spacing;
  int available_width = window_width - total_spacing;
  
  // Use 90% of available width to ensure we use most of the screen
  int optimal_card_width = (available_width * 90 / 100) / tableau_.size();
  
  // Calculate card height to maintain aspect ratio
  double card_aspect_ratio = static_cast<double>(BASE_CARD_HEIGHT) / BASE_CARD_WIDTH;
  int optimal_card_height = static_cast<int>(optimal_card_width * card_aspect_ratio);
  
  // Vertical space calculation
  int header_height = 60; // Height for menu bar
  int vertical_spacing = std::max(15, optimal_card_height / 5); // Minimum overlap for cascade
  
  // Check vertical space constraints
  // We want to ensure enough vertical space for a reasonable number of cascaded cards
  int max_cascade_cards = 12; // Maximum expected cascade in spider solitaire
  int required_height = header_height + optimal_card_height + 
                       (max_cascade_cards * vertical_spacing);
  
  // If required height exceeds window height, adjust accordingly
  if (required_height > window_height) {
    double vertical_scale = static_cast<double>(window_height - header_height) / 
                           (optimal_card_height + (max_cascade_cards * vertical_spacing));
    
    optimal_card_width = static_cast<int>(optimal_card_width * vertical_scale);
    optimal_card_height = static_cast<int>(optimal_card_height * vertical_scale);
    vertical_spacing = std::max(15, static_cast<int>(vertical_spacing * vertical_scale));
  }
  
  // Set reasonable limits on card size
  // Make minimum card size larger for better visibility
  optimal_card_width = std::min(optimal_card_width, 120); // Increased max width
  optimal_card_width = std::max(optimal_card_width, 80);  // Increased min width
  
  // Recalculate height with constrained width
  optimal_card_height = static_cast<int>(optimal_card_width * card_aspect_ratio);
  
  // Set the current dimensions
  current_card_width_ = optimal_card_width;
  current_card_height_ = optimal_card_height;
  current_card_spacing_ = std::max(min_spacing, optimal_card_width / 10); // Slightly larger spacing
  current_vert_spacing_ = vertical_spacing;
  
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
    } else {
      // Window not realized yet, try to get scale from display
      GdkDisplay *display = gdk_display_get_default();
      if (display) {
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        if (monitor) {
          display_scale = gdk_monitor_get_scale_factor(monitor);
        }
      }
    }
  }
  
  // Adjust window dimensions to logical pixels
  int logical_width = static_cast<int>(window_width / display_scale);
  int logical_height = static_cast<int>(window_height / display_scale);
  
  // Calculate scale factors for both dimensions using logical pixels
  double width_scale = static_cast<double>(logical_width) / BASE_WINDOW_WIDTH;
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
  
  // Disable keyboard navigation when auto-finish is active
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;

  // If any animation is currently running, wait for it to complete
  if (foundation_move_animation_active_ || deal_animation_active_ || 
      win_animation_active_ || stock_to_waste_animation_active_ || 
      sequence_animation_active_ || dragging_) {
    
    // Critical: added sequence_animation_active_ to the check
    // Set up a timer to check again after a longer delay if sequence animation is running
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
    }
    
    // Use a longer delay if sequence animation is running (500ms instead of 50ms)
    int delay = sequence_animation_active_ ? 500 : 50;
    auto_finish_timer_id_ = g_timeout_add(delay, onAutoFinishTick, this);
    return;
  }

  // Track previously seen tableau states to detect loops
  static std::unordered_map<std::string, int> seen_states;
  
  // Generate a hash of the current tableau state
  std::string state_hash;
  for (const auto& pile : tableau_) {
    for (const auto& card : pile) {
      if (card.face_up) {
        // Only include face-up cards in the hash
        state_hash += std::to_string(static_cast<int>(card.card.suit)) + 
                      std::to_string(static_cast<int>(card.card.rank)) + ":";
      } else {
        // Mark face-down cards differently
        state_hash += "X:";
      }
    }
    state_hash += "|"; // Separator between piles
  }
  
  // If we've seen this state before multiple times, we're in a loop
  if (seen_states[state_hash]++ > 2) {
    // We're stuck in a loop, exit auto-finish
    auto_finish_active_ = false;
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
      auto_finish_timer_id_ = 0;
    }
    seen_states.clear(); // Clear history for next time
    return;
  }
  
  bool found_move = false;
  
  // STEP 1: First, check each tableau pile for completed sequences (highest priority)
  for (size_t t = 0; t < tableau_.size() && !found_move; t++) {
    if (checkForCompletedSequence(t)) {
      found_move = true;
      // Reset seen states when we make progress
      seen_states.clear();
      
      // IMPORTANT: If we found a sequence to complete, schedule the next move with
      // a longer delay to allow the entire sequence animation to finish
      if (auto_finish_timer_id_ > 0) {
        g_source_remove(auto_finish_timer_id_);
      }
      // Use a longer delay (1000ms) after finding a sequence
      auto_finish_timer_id_ = g_timeout_add(1000, onAutoFinishTick, this);
      return; // Return immediately to let the sequence animation run
    }
  }
  
  // STEP 2: If no completed sequence, look for moves that expose face-down cards
  if (!found_move) {
    for (size_t source_pile_idx = 0; source_pile_idx < tableau_.size() && !found_move; source_pile_idx++) {
      auto& source_pile = tableau_[source_pile_idx];
      if (source_pile.empty()) {
        continue;
      }
      
      // Only consider moves that would expose a face-down card
      int face_up_count = 0;
      for (auto it = source_pile.rbegin(); it != source_pile.rend(); ++it) {
        if (it->face_up) {
          face_up_count++;
        } else {
          break;
        }
      }
      
      if (face_up_count > 0 && face_up_count < source_pile.size()) {
        // There's a face-down card that could be exposed
        int source_card_idx = source_pile.size() - face_up_count;
        int pile_index = source_pile_idx + 6;
        
        if (isValidDragSource(pile_index, source_card_idx)) {
          std::vector<cardlib::Card> cards_to_drag = getDragCards(pile_index, source_card_idx);
          
          for (size_t target_pile_idx = 0; target_pile_idx < tableau_.size() && !found_move; target_pile_idx++) {
            if (source_pile_idx == target_pile_idx) {
              continue;
            }
            
            auto& target_pile = tableau_[target_pile_idx];
            std::vector<cardlib::Card> target_cards;
            if (!target_pile.empty()) {
              target_cards = {target_pile.back().card};
            }
            
            if (canMoveToPile(cards_to_drag, target_cards, false)) {
              // In Spider, for auto-moves, only build same-suit sequences
              if (!target_pile.empty() && !cards_to_drag.empty() && 
                  target_pile.back().card.suit != cards_to_drag[0].suit) {
                continue;
              }
              
              // Execute move that exposes a face-down card
              executeMove(source_pile_idx, source_card_idx, target_pile_idx, cards_to_drag);
              found_move = true;
              seen_states.clear(); // Reset seen states when we make progress
              break;
            }
          }
        }
      }
    }
  }
  
  // STEP 3: Look for moves that build same-suit sequences
  if (!found_move) {
    struct MoveOption {
      size_t source_pile_idx;
      int source_card_idx;
      size_t target_pile_idx;
      std::vector<cardlib::Card> cards_to_drag;
      int sequence_length; // How long the resulting sequence would be
      int priority; // Overall priority score
    };
    
    std::vector<MoveOption> potential_moves;
    
    // Evaluate all possible moves
    for (size_t source_pile_idx = 0; source_pile_idx < tableau_.size(); source_pile_idx++) {
      auto& source_pile = tableau_[source_pile_idx];
      if (source_pile.empty()) {
        continue;
      }
      
      // For each face-up card in the pile
      for (int source_card_idx = 0; source_card_idx < source_pile.size(); source_card_idx++) {
        if (!source_pile[source_card_idx].face_up) {
          continue;
        }
        
        int pile_index = source_pile_idx + 6;
        if (isValidDragSource(pile_index, source_card_idx)) {
          std::vector<cardlib::Card> cards_to_drag = getDragCards(pile_index, source_card_idx);
          
          for (size_t target_pile_idx = 0; target_pile_idx < tableau_.size(); target_pile_idx++) {
            if (source_pile_idx == target_pile_idx) {
              continue;
            }
            
            auto& target_pile = tableau_[target_pile_idx];
            std::vector<cardlib::Card> target_cards;
            if (!target_pile.empty()) {
              target_cards = {target_pile.back().card};
            }
            
            if (canMoveToPile(cards_to_drag, target_cards, false)) {
              MoveOption move;
              move.source_pile_idx = source_pile_idx;
              move.source_card_idx = source_card_idx;
              move.target_pile_idx = target_pile_idx;
              move.cards_to_drag = cards_to_drag;
              
              // Calculate how beneficial this move would be
              int priority = 0;
              int sequence_length = 0;
              
              // If target is not empty, calculate sequence improvement
              if (!target_pile.empty()) {
                // Calculate existing sequence length in target
                cardlib::Suit target_suit = target_pile.back().card.suit;
                cardlib::Rank last_rank = target_pile.back().card.rank;
                
                // Count backward from the end of the pile
                int existing_sequence = 1; // Start with the top card
                for (int i = target_pile.size() - 2; i >= 0; i--) {
                  if (!target_pile[i].face_up) break;
                  
                  if (target_pile[i].card.suit == target_suit && 
                      static_cast<int>(target_pile[i].card.rank) == static_cast<int>(last_rank) + 1) {
                    existing_sequence++;
                    last_rank = target_pile[i].card.rank;
                  } else {
                    break;
                  }
                }
                
                // Calculate potential sequence after move
                if (cards_to_drag[0].suit == target_suit) {
                  // This move extends an existing sequence
                  sequence_length = existing_sequence + cards_to_drag.size();
                  
                  // Higher priority for longer sequences
                  priority += sequence_length * 10;
                  
                  // Bonus if this will make a complete K-A sequence (13 cards)
                  if (sequence_length >= 13) {
                    priority += 1000;
                  }
                } else {
                  // This move doesn't extend an existing sequence
                  // Only count the sequence in the cards being moved
                  sequence_length = cards_to_drag.size();
                  
                  // Discourage moving single cards between non-matching suits
                  // unless it would expose a face-down card
                  if (cards_to_drag.size() == 1 && source_card_idx > 0 && 
                      source_card_idx == source_pile.size() - 1) {
                    priority -= 50; // Penalty for pointless single-card moves
                  }
                }
              } else {
                // Moving to an empty pile
                // For empty piles, prioritize kings or longer sequences
                if (cards_to_drag[0].rank == cardlib::Rank::KING) {
                  priority += 30; // Good to move Kings to empty spots
                }
                sequence_length = cards_to_drag.size();
              }
              
              // Bonus for exposing a face-down card
              if (source_card_idx == 0 && source_pile.size() > cards_to_drag.size()) {
                if (!source_pile[cards_to_drag.size()].face_up) {
                  priority += 500; // Very high priority for revealing cards
                }
              }
              
              move.sequence_length = sequence_length;
              move.priority = priority;
              
              potential_moves.push_back(move);
            }
          }
        }
      }
    }
    
    // Sort by priority (highest first)
    std::sort(potential_moves.begin(), potential_moves.end(), 
             [](const MoveOption& a, const MoveOption& b) {
               return a.priority > b.priority;
             });
    
    // Execute the best move if any exists
    if (!potential_moves.empty()) {
      const auto& best_move = potential_moves[0];
      
      // Only make moves that are actually beneficial (positive priority)
      // or if we haven't found other moves to make
      if (best_move.priority > 0 || potential_moves.size() == 1) {
        executeMove(best_move.source_pile_idx, best_move.source_card_idx, 
                   best_move.target_pile_idx, best_move.cards_to_drag);
        found_move = true;
      }
    }
  }

  // STEP 4: Try to draw cards from stock if no other moves are available and all columns have cards
  // Not implemented in this version as the original code commented it out

  if (found_move) {
    // Set up a timer to check for the next move after a short delay
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
    seen_states.clear(); // Clear history for next time

    // Check if the player has won
    if (checkWinCondition()) {
      startWinAnimation();
    }
  }
}

// Add this function declaration to solitaire.h:
// void executeMove(size_t source_pile_idx, int source_card_idx, size_t target_pile_idx, const std::vector<cardlib::Card>& cards_to_drag);

// Helper method to execute a move between tableau piles
void SolitaireGame::executeMove(size_t source_pile_idx, int source_card_idx, 
                               size_t target_pile_idx, const std::vector<cardlib::Card>& cards_to_drag) {
  auto& source_pile = tableau_[source_pile_idx];
  auto& target_pile = tableau_[target_pile_idx];
  
  // Set up drag state for animation
  dragging_ = true;
  drag_source_pile_ = source_pile_idx + 6;
  drag_cards_ = cards_to_drag;
  
  // Calculate positions for display
  int source_x = current_card_spacing_ + 
                source_pile_idx * (current_card_width_ + current_card_spacing_);
  int source_y = (current_card_spacing_ + current_card_height_ + 
                current_vert_spacing_) + source_card_idx * current_vert_spacing_;
                
  int target_x = current_card_spacing_ + 
                target_pile_idx * (current_card_width_ + current_card_spacing_);
  int target_y = (current_card_spacing_ + current_card_height_ + 
                current_vert_spacing_);
  if (!target_pile.empty()) {
    target_y += (target_pile.size() - 1) * current_vert_spacing_;
  }
  
  // Set drag positions for visual feedback
  drag_start_x_ = target_x;
  drag_start_y_ = target_y;
  drag_offset_x_ = 0;
  drag_offset_y_ = 0;
  
  // Play sound
  playSound(GameSoundEvent::CardPlace);
  
  // Remove cards from source pile
  int cards_to_remove = source_pile.size() - source_card_idx;
  source_pile.erase(source_pile.begin() + source_card_idx, source_pile.end());
  
  // Flip the new top card in the source pile if needed
  if (!source_pile.empty() && !source_pile.back().face_up) {
    source_pile.back().face_up = true;
    playSound(GameSoundEvent::CardFlip);
  }
  
  // Add cards to target pile
  for (const auto& card : cards_to_drag) {
    target_pile.emplace_back(card, true);
  }
  
  // Reset drag state
  dragging_ = false;
  drag_cards_.clear();
  drag_source_pile_ = -1;
  
  // Check if this move created a completed sequence
  checkForCompletedSequence(target_pile_idx);
  
  // Force a redraw
  refreshDisplay();
}

gboolean SolitaireGame::onAutoFinishTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  
  // Reset the timer ID before processing the next move
  game->auto_finish_timer_id_ = 0;
  
  // Process the next move
  game->processNextAutoFinishMove();
  
  // Always return FALSE to ensure this timer doesn't repeat on its own
  return FALSE;
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


bool SolitaireGame::checkForCompletedSequence(int tableau_index) {
    if (tableau_index < 0 || tableau_index >= tableau_.size()) {
        return false;
    }
    
    if (sequence_animation_active_) {
        return false;  // Don't process this sequence yet
    }
    
    auto& pile = tableau_[tableau_index];
    
    // Need at least 13 cards for a complete sequence
    if (pile.size() < 13) {
        return false;
    }
    
    // Start from the end of the pile (the top-most card visible to player)
    int top_position = pile.size() - 1;
    
    // We need to find Ace at the top position
    if (pile[top_position].card.rank != cardlib::Rank::ACE) {
        return false;
    }
    
    // Check if we have 13 cards in descending sequence of the same suit
    cardlib::Suit suit = pile[top_position].card.suit;
    
    for (int i = 0; i < 13; i++) {
        int pos = top_position - i;
        if (pos < 0) return false; // Not enough cards
        
        // Check current card
        if (!pile[pos].face_up || 
            pile[pos].card.suit != suit || 
            static_cast<int>(pile[pos].card.rank) != (1 + i)) { // ACE=1, 2=2, ... KING=13
            return false;
        }
    }
    
    // Found a valid sequence!
    // Start the sequence animation - this will handle removing cards and updating the foundation
    startSequenceAnimation(tableau_index);
    
    // The animation has started - don't remove cards here
    return true;
}


void SolitaireGame::promptForNewGame(const std::string& difficulty) {
  // Ask if the user wants to start a new game with the new difficulty
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_),
      GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_YES_NO,
      "Start a new game with %s difficulty?", 
      difficulty.c_str());
  
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  
  if (response == GTK_RESPONSE_YES) {
    saveSettings();
    restartGame();
  }
}

// ============================================================================
// OPENGL RENDERING BACKEND IMPLEMENTATION (STUB METHODS)
// ============================================================================
// These methods are called when USEOPENGL is defined and OpenGL rendering
// is enabled. They follow the same rendering pipeline as Cairo methods.

#ifdef USEOPENGL

bool SolitaireGame::initializeOpenGLResources() {
  if (!initializeGLEW_gl()) {
    return false;
  }
  
  if (!checkOpenGLCapabilities_gl()) {
    return false;
  }
  
  logOpenGLInfo_gl();
  
  if (!initializeCardTextures_gl()) {
    return false;
  }
  
  opengl_initialized_ = true;
  return true;
}

gboolean SolitaireGame::onGLRealize(GtkGLArea *area, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  gtk_gl_area_make_current(area);
  
  if (game->initializeOpenGLResources()) {
    game->cardShaderProgram_gl_ = game->setupShaders_gl();
    game->cardQuadVAO_gl_ = game->setupCardQuadVAO_gl();
    return TRUE;
  }
  return FALSE;
}

gboolean SolitaireGame::onGLRender(GtkGLArea *area, GdkGLContext *context, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  gtk_gl_area_make_current(area);
  game->renderFrame_gl();
  return TRUE;
}


#endif // USEOPENGL
