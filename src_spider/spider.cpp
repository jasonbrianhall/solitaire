#include "solitaire.h"
#include "spiderdeck.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
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
      sound_enabled_(true),           // Set sound to enabled by default
      sounds_zip_path_("sound.zip"),
      current_seed_(0) { // Initialize to 0 temporarily
  srand(time(NULL));  // Seed the random number generator with current time
  current_seed_ = rand();  // Generate random seed
  initializeGame();
  initializeSettingsDir();
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
  gtk_main();
}

void SolitaireGame::initializeGame() {
  try {
    // Try to find cards.zip in several common locations
    const std::vector<std::string> paths = {"cards.zip"};

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
    
    // Check if this card and all cards above it form a valid sequence to drag
    // In Spider, you can drag any sequence of cards, but they must be in descending order
    // If they're all the same suit, you can drag them as a block regardless of sequence
    for (size_t i = card_index; i < pile.size() - 1; i++) {
      // Check if current card and next card have consecutive ranks
      bool consecutive_rank = static_cast<int>(pile[i].card.rank) == 
                             static_cast<int>(pile[i+1].card.rank) + 1;
      
      // Check if they are the same suit
      bool same_suit = pile[i].card.suit == pile[i+1].card.suit;
      
      // In Spider, you can drag cards of any suit, but they must be in descending sequence
      // If they're not in a valid sequence, you can only drag if they're the same suit
      if (!consecutive_rank && !same_suit) {
        return false;
      }
    }
    
    return true;
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
  // Determine number of decks based on difficulty
  int num_suits = 1;  // Default to single suit (difficult)
  
  // Clear all piles
  stock_.clear();
  tableau_.clear();
  foundation_.clear(); 

  // Create the appropriate Spider Deck
  cardlib::SpiderDeck spider_deck(num_suits);
  
  // Shuffle the decks
  spider_deck.shuffle();

  // Resize tableau to 10 piles
  tableau_.resize(10);

  // Deal initial tableau
  // First 6 piles get 6 cards each
  // Last 4 piles get 5 cards each
  for (int i = 0; i < 10; i++) {
    int cards_to_deal = (i < 6) ? 6 : 5;
    
    for (int j = 0; j < cards_to_deal; j++) {
      if (auto card = spider_deck.drawCard()) {
        // First card in each pile is face up, rest are face down
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
    // Handle tableau piles
    const auto &tableau_pile = tableau_[pile_index - 6];
    if (card_index >= 0 &&
        static_cast<size_t>(card_index) < tableau_pile.size() &&
        tableau_pile[card_index].face_up) {
        
      // In Spider, we need to determine if the cards form a valid draggable sequence
      // Valid sequences are:
      // 1. Descending rank (regardless of suit)
      // 2. Same suit (can be dragged as a block)
      
      // First, check if all the cards above the starting card are the same suit
      bool all_same_suit = true;
      cardlib::Suit first_suit = tableau_pile[card_index].card.suit;
      
      for (size_t i = card_index; i < tableau_pile.size(); i++) {
        if (tableau_pile[i].card.suit != first_suit) {
          all_same_suit = false;
          break;
        }
      }
      
      // If all cards are the same suit, we can drag them all regardless of sequence
      if (all_same_suit) {
        for (size_t i = card_index; i < tableau_pile.size(); i++) {
          result.push_back(tableau_pile[i].card);
        }
        return result;
      }
      
      // Otherwise, we need to check for a valid descending sequence
      size_t end_index = card_index;
      cardlib::Rank current_rank = tableau_pile[card_index].card.rank;
      
      // Find the longest valid sequence from card_index
      for (size_t i = card_index + 1; i < tableau_pile.size(); i++) {
        cardlib::Rank next_rank = tableau_pile[i].card.rank;
        
        // Check if the next card is one rank lower
        if (static_cast<int>(next_rank) == static_cast<int>(current_rank) - 1) {
          end_index = i;
          current_rank = next_rank;
        } else {
          // Sequence broken
          break;
        }
      }
      
      // Add all cards in the valid sequence to the result
      for (size_t i = card_index; i <= end_index; i++) {
        result.push_back(tableau_pile[i].card);
      }
    }
    return result;
  } 
  
  // For non-tableau piles (not used in Spider)
  return std::vector<cardlib::Card>();
}

gboolean SolitaireGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                      gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

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
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    // Try to move card to foundation
    if (pile_index >= 0) {
      const cardlib::Card *card = nullptr;
      int target_foundation = -1;

      // Get the card based on pile type
      if (pile_index == 1 && !game->waste_.empty()) {
        card = &game->waste_.back();
        // Find which foundation to move to
        for (int i = 0; i < game->foundation_.size(); i++) {
          if (game->canMoveToFoundation(*card, i)) {
            target_foundation = i;
            break;
          }
        }

        if (target_foundation >= 0) {
          // Start animation
          game->startFoundationMoveAnimation(*card, pile_index, 0,
                                             target_foundation + 2);
          game->playSound(GameSoundEvent::CardPlace);
          // Remove card from waste pile
          game->waste_.pop_back();

          // The card will be added to the foundation in
          // updateFoundationMoveAnimation when animation completes
          return TRUE;
        }
      } else if (pile_index >= 6 && pile_index <= 12) {
        auto &tableau_pile = game->tableau_[pile_index - 6];
        if (!tableau_pile.empty() && tableau_pile.back().face_up) {
          card = &tableau_pile.back().card;

          // Find which foundation to move to
          for (int i = 0; i < game->foundation_.size(); i++) {
            if (game->canMoveToFoundation(*card, i)) {
              target_foundation = i;
              break;
            }
          }

          if (target_foundation >= 0) {
            // Start animation
            game->startFoundationMoveAnimation(*card, pile_index,
                                               tableau_pile.size() - 1,
                                               target_foundation + 2);
            game->playSound(GameSoundEvent::CardPlace);

            // Remove card from tableau
            tableau_pile.pop_back();

            // Flip new top card if needed
            if (!tableau_pile.empty() && !tableau_pile.back().face_up) {
              // game->playSound(GameSoundEvent::CardFlip);

              tableau_pile.back().face_up = true;
            }

            // The card will be added to the foundation in
            // updateFoundationMoveAnimation when animation completes
            return TRUE;
          }
        }
      }
    }

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
  for (const auto& pile : tableau_) {
    if (pile.empty()) {
      can_deal = false;
      break;
    }
  }

  if (!can_deal) {
    // Show message that all tableau piles must have at least one card
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, 
        "All tableau piles must have at least one card before dealing more cards.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }

  // Deal one card to each tableau pile (10 cards total for Spider)
  if (stock_.size() >= tableau_.size()) {
    // Start animation instead of immediately moving cards
    if (!stock_to_waste_animation_active_) {
      // Deal 10 cards - one to each tableau pile
      for (int i = 0; i < tableau_.size(); i++) {
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
      
      refreshDisplay();
    }
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
  
  // Foundation pile rules remain the same for compatibility
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

  // In Spider, cards must just be one rank lower (suit doesn't matter)
  // For example, you can place any Queen on any King
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
  // Check if all foundation piles have 13 cards
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
  gtk_window_set_title(GTK_WINDOW(window_), "Solitaire");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit),
                   NULL);

  gtk_widget_add_events(window_, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window_), "key-press-event", G_CALLBACK(onKeyPress),
                   this);

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

  // Game menu
  GtkWidget *gameMenu = gtk_menu_new();
  GtkWidget *gameMenuItem = gtk_menu_item_new_with_mnemonic("_Game");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

  // New Game
  GtkWidget *newGameItem =
      gtk_menu_item_new_with_mnemonic("_New Game (CTRL+N)");
  g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

GtkWidget *restartGameItem = gtk_menu_item_new_with_label("Restart Game");
g_signal_connect(G_OBJECT(restartGameItem), "activate", 
                G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                  static_cast<SolitaireGame *>(data)->restartGame();
                }), 
                this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), restartGameItem);

// Add a Custom Seed option
GtkWidget *seedItem = gtk_menu_item_new_with_label("Enter Seed...");
g_signal_connect(G_OBJECT(seedItem), "activate", 
                G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                  static_cast<SolitaireGame *>(data)->promptForSeed();
                }), 
                this);
gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), seedItem);

#ifdef DEBUG
  GtkWidget *testLayoutItem = gtk_menu_item_new_with_label("Test Layout");
  g_signal_connect(G_OBJECT(testLayoutItem), "activate",
                   G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
                     game->dealTestLayout();
                     game->refreshDisplay();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), testLayoutItem);
#endif

  // Draw Mode submenu
  GtkWidget *drawModeItem = gtk_menu_item_new_with_mnemonic("_Draw Mode");
  GtkWidget *drawModeMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(drawModeItem), drawModeMenu);

  // Draw One option
  GtkWidget *drawOneItem =
      gtk_radio_menu_item_new_with_mnemonic(NULL, "_One (1)");
  GSList *group =
      gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(drawOneItem));
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
  GtkWidget *drawThreeItem =
      gtk_radio_menu_item_new_with_mnemonic(group, "_Three (3)");
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

  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), drawModeItem);

  // Card Back menu
  GtkWidget *cardBackMenu = gtk_menu_new();
  GtkWidget *cardBackItem = gtk_menu_item_new_with_mnemonic("_Card Back");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(cardBackItem), cardBackMenu);

  // Select custom back option
  GtkWidget *selectBackItem =
      gtk_menu_item_new_with_mnemonic("_Select Custom Back");
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
          char *filename =
              gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
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
  GtkWidget *resetBackItem =
      gtk_menu_item_new_with_mnemonic("_Reset to Default Back");
  g_signal_connect(G_OBJECT(resetBackItem), "activate",
                   G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
                     game->resetToDefaultBack();
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(cardBackMenu), resetBackItem);

  // Add the Card Back submenu to the Game menu
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), cardBackItem);

  // Load Deck option
  GtkWidget *loadDeckItem = gtk_menu_item_new_with_mnemonic("_Load Deck (CTRL+L)");
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
          char *filename =
              gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
          if (game->loadDeck(filename)) {
            game->refreshDisplay();
          }
          g_free(filename);
        }

        gtk_widget_destroy(dialog);
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), loadDeckItem);

  // Fullscreen option
  GtkWidget *fullscreenItem =
      gtk_menu_item_new_with_mnemonic("Toggle _Fullscreen (F11)");
  g_signal_connect(G_OBJECT(fullscreenItem), "activate",
                   G_CALLBACK(onToggleFullscreen), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), fullscreenItem);

  // Separator
  GtkWidget *sep = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep);

  GtkWidget *soundItem = gtk_check_menu_item_new_with_mnemonic("_Sound");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(soundItem),
                                 sound_enabled_);
  g_signal_connect(G_OBJECT(soundItem), "toggled",
                   G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     SolitaireGame *game = static_cast<SolitaireGame *>(data);
                     game->sound_enabled_ = gtk_check_menu_item_get_active(
                         GTK_CHECK_MENU_ITEM(widget));
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), soundItem);

  // Add a separator before Quit item
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep);

  // Quit
  GtkWidget *quitItem = gtk_menu_item_new_with_mnemonic("_Quit (CTRL+Q)");
  g_signal_connect(G_OBJECT(quitItem), "activate", G_CALLBACK(onQuit), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), quitItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), gameMenuItem);

  // Help menu
  GtkWidget *helpMenu = gtk_menu_new();
  GtkWidget *helpMenuItem = gtk_menu_item_new_with_mnemonic("_Help");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMenuItem), helpMenu);

  // About
  GtkWidget *aboutItem = gtk_menu_item_new_with_mnemonic("_About (CTRL+H)");
  g_signal_connect(G_OBJECT(aboutItem), "activate", G_CALLBACK(onAbout), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutItem);

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMenuItem);
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
  stock_.clear();
  waste_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset foundation and tableau
  foundation_.resize(4);
  tableau_.resize(7);

  // Set up each suit in order in the tableau
  for (int suit = 0; suit < 4; suit++) {
    // Add 13 cards of this suit to a vector in reverse order (King to Ace)
    std::vector<cardlib::Card> suit_cards;
    for (int rank = static_cast<int>(cardlib::Rank::KING);
         rank >= static_cast<int>(cardlib::Rank::ACE); rank--) {
      suit_cards.emplace_back(static_cast<cardlib::Suit>(suit),
                              static_cast<cardlib::Rank>(rank));
    }

    // Distribute the cards to tableau
    for (size_t i = 0; i < suit_cards.size(); i++) {
      tableau_[i % 7].emplace_back(suit_cards[i], true); // All cards face up
    }
  }
}

void SolitaireGame::initializeSettingsDir() {
  const char *home_dir = nullptr;
  std::string app_dir;

#ifdef _WIN32
  home_dir = getenv("USERPROFILE");
  if (home_dir) {
    app_dir = std::string(home_dir) + "\\AppData\\Local\\Solitaire";
    // app_dir = std::string(home_dir) + "/.config/solitaire";
  }
#else
  home_dir = getenv("HOME");
  if (home_dir) {
    app_dir = std::string(home_dir) + "/.config/solitaire";
  }
#endif

  if (!home_dir) {
    std::cerr << "Could not determine home directory" << std::endl;
    return;
  }

  settings_dir_ = app_dir;

// Create directory if it doesn't exist
#ifdef _WIN32
  _mkdir(app_dir.c_str());
#else
  mkdir(app_dir.c_str(), 0755);
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

  bool settings_loaded = false;
  std::string line;
  while (std::getline(file, line)) {
    if (line.substr(0, 10) == "card_back=") {
      custom_back_path_ = line.substr(10);
      settings_loaded = true;
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

double SolitaireGame::getScaleFactor(int window_width,
                                     int window_height) const {
  // Calculate scale factors for both dimensions
  double width_scale = static_cast<double>(window_width) / BASE_WINDOW_WIDTH;
  double height_scale = static_cast<double>(window_height) / BASE_WINDOW_HEIGHT;

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

bool SolitaireGame::checkForCompletedSequence(int tableau_index) {
  if (tableau_index < 0 || tableau_index >= tableau_.size()) {
    return false;
  }
  
  const auto& pile = tableau_[tableau_index];
  
  // Need at least 13 cards for a complete sequence
  if (pile.size() < 13) {
    return false;
  }
  
  // In Spider, we need to find a King-to-Ace sequence of the same suit
  // Look for a sequence starting with a King at the top
  size_t start_pos = pile.size() - 1;
  
  // Start looking from the end of the pile
  while (start_pos >= 12) { // Need at least 13 cards for a K-A sequence
    // Must start with a King
    if (pile[start_pos].card.rank == cardlib::Rank::KING) {
      // Found a King, check for a sequence
      cardlib::Suit suit = pile[start_pos].card.suit;
      bool complete_sequence = true;
      
      // Check for the descending sequence: K, Q, J, 10, 9, 8, 7, 6, 5, 4, 3, 2, A
      for (int i = 0; i < 13; i++) {
        size_t pos = start_pos - i;
        int expected_rank = static_cast<int>(cardlib::Rank::KING) - i;
        
        // Check if the card matches the expected rank and suit
        if (static_cast<int>(pile[pos].card.rank) != expected_rank ||
            pile[pos].card.suit != suit ||
            !pile[pos].face_up) {
          complete_sequence = false;
          break;
        }
      }
      
      if (complete_sequence) {
        // We found a complete sequence!
        // Remove the 13 cards from the tableau pile
        tableau_[tableau_index].erase(
          tableau_[tableau_index].begin() + (start_pos - 12),
          tableau_[tableau_index].begin() + start_pos + 1
        );
        
        // If there are still cards in the pile, make sure the top one is face up
        if (!tableau_[tableau_index].empty() && !tableau_[tableau_index].back().face_up) {
          tableau_[tableau_index].back().face_up = true;
          playSound(GameSoundEvent::CardFlip);
        }
        
        // Add to foundation count (we just track completed sequences by adding to pile 0)
        foundation_[0].push_back(cardlib::Card(suit, cardlib::Rank::KING));
        
        // Play the completion sound
        playSound(GameSoundEvent::WinGame);
        
        refreshDisplay();
        
        return true;
      }
    }
    
    // Move up the pile to continue checking
    start_pos--;
  }
  
  return false;
}
