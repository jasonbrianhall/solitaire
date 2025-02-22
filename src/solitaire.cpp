#include "solitaire.h"
#include <algorithm>
#include <iostream>

SolitaireGame::SolitaireGame()
    : dragging_(false), drag_source_(nullptr), drag_source_pile_(-1),
      window_(nullptr), game_area_(nullptr) {
  initializeGame();
}

SolitaireGame::~SolitaireGame() {
  // GTK cleanup will handle widget destruction
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
    const std::vector<std::string> paths = {
      "cards.zip"
    };

    bool loaded = false;
    for (const auto& path : paths) {
      try {
        deck_ = cardlib::Deck(path);
        deck_.removeJokers();
        loaded = true;
        break;
      } catch (const std::exception& e) {
        std::cerr << "Failed to load cards from " << path << ": " << e.what() << std::endl;
      }
    }

    if (!loaded) {
      throw std::runtime_error("Could not find cards.zip in any search path");
    }
    
    deck_.shuffle();

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

  } catch (const std::exception& e) {
    std::cerr << "Fatal error during game initialization: " << e.what() << std::endl;
    exit(1);
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

    // Can drag from foundation only top card
    if (pile_index >= 2 && pile_index <= 5) {
        const auto& pile = foundation_[pile_index - 2];
        return !pile.empty() && static_cast<size_t>(card_index) == pile.size() - 1;
    }

    // Can drag from tableau if cards are face up
    if (pile_index >= 6 && pile_index <= 12) {
        const auto& pile = tableau_[pile_index - 6];
        return !pile.empty() && 
               card_index >= 0 &&
               static_cast<size_t>(card_index) < pile.size() &&
               pile[card_index].face_up;  // Make sure card is face up
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

void SolitaireGame::drawCard(cairo_t* cr, int x, int y, const cardlib::CardImage* img) const {
    if (!img || img->data.empty()) {
        std::cerr << "Invalid card image data" << std::endl;
        return;
    }

    GError* error = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!loader) {
        std::cerr << "Failed to create pixbuf loader" << std::endl;
        return;
    }

    // Write the image data
    if (!gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(), &error)) {
        std::cerr << "Failed to write image data: " << (error ? error->message : "unknown error") << std::endl;
        if (error) g_error_free(error);
        g_object_unref(loader);
        return;
    }

    // Close the loader
    if (!gdk_pixbuf_loader_close(loader, &error)) {
        std::cerr << "Failed to close loader: " << (error ? error->message : "unknown error") << std::endl;
        if (error) g_error_free(error);
        g_object_unref(loader);
        return;
    }

    // Get the pixbuf and scale it
    GdkPixbuf* original_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (original_pixbuf) {
        // Scale the pixbuf to our desired card size
        GdkPixbuf* scaled_pixbuf = gdk_pixbuf_scale_simple(
            original_pixbuf,
            CARD_WIDTH,
            CARD_HEIGHT,
            GDK_INTERP_BILINEAR
        );

        if (scaled_pixbuf) {
            // Draw the scaled pixbuf
            gdk_cairo_set_source_pixbuf(cr, scaled_pixbuf, x, y);
            cairo_paint(cr);
            g_object_unref(scaled_pixbuf);
        }
    }

    // Clean up
    g_object_unref(loader);  // This will also free the original pixbuf
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
      }
    }
    // Deal one card face up at the end
    if (auto card = deck_.drawCard()) {
      tableau_[i].emplace_back(*card, true); // face up
    }
  }

  // Move remaining cards to stock (face down)
  while (auto card = deck_.drawCard()) {
    stock_.push_back(*card);
  }
}

void SolitaireGame::flipTopTableauCard(int pile_index) {
  if (pile_index < 0 || pile_index >= static_cast<int>(tableau_.size())) {
    return;
  }

  auto &pile = tableau_[pile_index];
  if (!pile.empty() && !pile.back().face_up) {
    pile.back().face_up = true;
  }
}

void SolitaireGame::setupWindow() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Solitaire");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit),
                   NULL);
}

void SolitaireGame::setupGameArea() {
  game_area_ = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(window_), game_area_);

  // Enable mouse event handling
  gtk_widget_add_events(game_area_, GDK_BUTTON_PRESS_MASK |
                                        GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK);

  // Connect signals
  g_signal_connect(G_OBJECT(game_area_), "draw", G_CALLBACK(onDraw), this);
  g_signal_connect(G_OBJECT(game_area_), "button-press-event",
                   G_CALLBACK(onButtonPress), this);
  g_signal_connect(G_OBJECT(game_area_), "button-release-event",
                   G_CALLBACK(onButtonRelease), this);
  g_signal_connect(G_OBJECT(game_area_), "motion-notify-event",
                   G_CALLBACK(onMotionNotify), this);

  gtk_widget_show_all(window_);
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

gboolean SolitaireGame::onDraw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    SolitaireGame* game = static_cast<SolitaireGame*>(data);
    
    // Clear background
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    cairo_set_source_rgb(cr, 0.0, 0.5, 0.0);  // Green table
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    // Draw stock pile
    int x = CARD_SPACING;
    int y = CARD_SPACING;
    if (!game->stock_.empty()) {
        if (auto back_img = game->deck_.getCardBackImage()) {
            game->drawCard(cr, x, y, &(*back_img));
        }
    } else {
        // Draw empty stock pile outline
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_rectangle(cr, x, y, CARD_WIDTH, CARD_HEIGHT);
        cairo_stroke(cr);
    }

    // Draw waste pile
    x += CARD_WIDTH + CARD_SPACING;
    if (!game->waste_.empty()) {
        const auto& top_card = game->waste_.back();
        if (auto img = game->deck_.getCardImage(top_card)) {
            game->drawCard(cr, x, y, &(*img));
        }
    }

    // Draw foundation piles
    x = 3 * (CARD_WIDTH + CARD_SPACING);
    for (const auto& pile : game->foundation_) {
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_rectangle(cr, x, y, CARD_WIDTH, CARD_HEIGHT);
        cairo_stroke(cr);
        
        if (!pile.empty()) {
            const auto& top_card = pile.back();
            if (auto img = game->deck_.getCardImage(top_card)) {
                game->drawCard(cr, x, y, &(*img));
            }
        }
        x += CARD_WIDTH + CARD_SPACING;
    }

    // Draw tableau piles
    const int tableau_base_y = CARD_SPACING + CARD_HEIGHT + VERT_SPACING;
    for (size_t i = 0; i < game->tableau_.size(); i++) {
        x = CARD_SPACING + i * (CARD_WIDTH + CARD_SPACING);
        const auto& pile = game->tableau_[i];
        
        for (size_t j = 0; j < pile.size(); j++) {
            // Don't draw cards that are being dragged
            if (game->dragging_ && game->drag_source_pile_ >= 6 && 
                game->drag_source_pile_ - 6 == static_cast<int>(i) && 
                j >= static_cast<size_t>(game->tableau_[i].size() - game->drag_cards_.size())) {
                continue;
            }
            
            // Calculate y position relative to tableau base for each card
            int current_y = tableau_base_y + j * VERT_SPACING;
            
            const auto& tableau_card = pile[j];
            if (tableau_card.face_up) {
                if (auto img = game->deck_.getCardImage(tableau_card.card)) {
                    game->drawCard(cr, x, current_y, &(*img));
                }
            } else {
                if (auto back_img = game->deck_.getCardBackImage()) {
                    game->drawCard(cr, x, current_y, &(*back_img));
                }
            }
        }
    }

    // Draw dragged cards
    if (game->dragging_ && !game->drag_cards_.empty()) {
        int drag_x = static_cast<int>(game->drag_start_x_ - game->drag_offset_x_);
        int drag_y = static_cast<int>(game->drag_start_y_ - game->drag_offset_y_);
        
        for (size_t i = 0; i < game->drag_cards_.size(); i++) {
            if (auto img = game->deck_.getCardImage(game->drag_cards_[i])) {
                game->drawCard(cr, drag_x, drag_y + i * VERT_SPACING, &(*img));
            }
        }
    }

    return TRUE;
}


std::vector<cardlib::Card> SolitaireGame::getTableauCardsAsCards(
    const std::vector<TableauCard>& tableau_cards, int start_index) {
    std::vector<cardlib::Card> cards;
    for (size_t i = start_index; i < tableau_cards.size(); i++) {
        if (tableau_cards[i].face_up) {
            cards.push_back(tableau_cards[i].card);
        }
    }
    return cards;
}

std::vector<cardlib::Card> SolitaireGame::getDragCards(int pile_index, int card_index) {
    if (pile_index >= 6 && pile_index <= 12) {
        // Handle tableau piles
        const auto& tableau_pile = tableau_[pile_index - 6];
        if (card_index >= 0 && static_cast<size_t>(card_index) < tableau_pile.size() && 
            tableau_pile[card_index].face_up) {
            return getTableauCardsAsCards(tableau_pile, card_index);
        }
        return std::vector<cardlib::Card>();
    }
    
    // Handle other piles using getPileReference
    auto& pile = getPileReference(pile_index);
    if (card_index >= 0 && static_cast<size_t>(card_index) < pile.size()) {
        return std::vector<cardlib::Card>(pile.begin() + card_index, pile.end());
    }
    return std::vector<cardlib::Card>();
}

gboolean SolitaireGame::onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    SolitaireGame* game = static_cast<SolitaireGame*>(data);

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
            game->drag_offset_x_ = event->x - (CARD_SPACING + (pile_index >= 6 ? (pile_index - 6) : (pile_index == 1 ? 1 : pile_index)) * (CARD_WIDTH + CARD_SPACING));
            game->drag_offset_y_ = event->y - (pile_index >= 6 ? (CARD_SPACING + CARD_HEIGHT + VERT_SPACING + card_index * VERT_SPACING) : CARD_SPACING);
        }
    }

    return TRUE;
}

gboolean SolitaireGame::onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    SolitaireGame* game = static_cast<SolitaireGame*>(data);

    if (event->button == 1 && game->dragging_) {
        auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

        if (target_pile >= 0) {
            bool move_successful = false;
            
            // Handle dropping on tableau piles (index 6-12)
            if (target_pile >= 6 && target_pile <= 12) {
                auto& tableau_pile = game->tableau_[target_pile - 6];
                if (game->canMoveToPile(game->drag_cards_, 
                    tableau_pile.empty() ? std::vector<cardlib::Card>() : 
                    std::vector<cardlib::Card>{tableau_pile.back().card})) {
                    
                    // Remove cards from source
                    if (game->drag_source_pile_ >= 6 && game->drag_source_pile_ <= 12) {
                        auto& source_tableau = game->tableau_[game->drag_source_pile_ - 6];
                        source_tableau.erase(source_tableau.end() - game->drag_cards_.size(), source_tableau.end());
                        
                        // Flip over the new top card if there is one
                        if (!source_tableau.empty() && !source_tableau.back().face_up) {
                            source_tableau.back().face_up = true;
                        }
                    } else {
                        auto& source = game->getPileReference(game->drag_source_pile_);
                        source.erase(source.end() - game->drag_cards_.size(), source.end());
                    }

                    // Add cards to target tableau
                    for (const auto& card : game->drag_cards_) {
                        tableau_pile.emplace_back(card, true);
                    }
                    move_successful = true;
                }
            }
            // Handle dropping on foundation piles (index 2-5)
            else if (target_pile >= 2 && target_pile <= 5) {
                if (game->drag_cards_.size() == 1) {  // Only allow single cards
                    auto& foundation_pile = game->foundation_[target_pile - 2];
                    if (game->canMoveToPile(game->drag_cards_, foundation_pile)) {
                        // Remove card from source
                        if (game->drag_source_pile_ >= 6 && game->drag_source_pile_ <= 12) {
                            auto& source_tableau = game->tableau_[game->drag_source_pile_ - 6];
                            source_tableau.pop_back();  // Remove single card
                            
                            // Flip over the new top card if there is one
                            if (!source_tableau.empty() && !source_tableau.back().face_up) {
                                source_tableau.back().face_up = true;
                            }
                        } else {
                            auto& source = game->getPileReference(game->drag_source_pile_);
                            source.pop_back();
                        }

                        // Add to foundation
                        foundation_pile.push_back(game->drag_cards_[0]);
                        move_successful = true;
                    }
                }
            }

            // Only check win condition if a move was actually made
            if (move_successful && game->checkWinCondition()) {
                GtkWidget* dialog = gtk_message_dialog_new(
                    GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Congratulations! You've won!");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);

                game->initializeGame();
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
        // If stock is empty, move all waste cards back to stock in reverse order
        while (!waste_.empty()) {
            stock_.push_back(waste_.back());
            waste_.pop_back();
        }
    } else {
        // Deal up to 3 cards from stock to waste
        for (int i = 0; i < 3 && !stock_.empty(); i++) {
            waste_.push_back(stock_.back());
            stock_.pop_back();
        }
    }
    refreshDisplay();
}

gboolean SolitaireGame::onMotionNotify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    SolitaireGame* game = static_cast<SolitaireGame*>(data);

    if (game->dragging_) {
        game->drag_start_x_ = event->x;
        game->drag_start_y_ = event->y;
        gtk_widget_queue_draw(game->game_area_);
    }

    return TRUE;
}

std::pair<int, int> SolitaireGame::getPileAt(int x, int y) const {
    // Check stock pile
    if (x >= CARD_SPACING && x <= CARD_SPACING + CARD_WIDTH &&
        y >= CARD_SPACING && y <= CARD_SPACING + CARD_HEIGHT) {
        return {0, stock_.empty() ? -1 : 0};
    }

    // Check waste pile
    if (x >= 2 * CARD_SPACING + CARD_WIDTH &&
        x <= 2 * CARD_SPACING + 2 * CARD_WIDTH && y >= CARD_SPACING &&
        y <= CARD_SPACING + CARD_HEIGHT) {
        return {1, waste_.empty() ? -1 : static_cast<int>(waste_.size() - 1)};
    }

    // Check foundation piles
    int foundation_x = 3 * (CARD_WIDTH + CARD_SPACING);
    for (int i = 0; i < 4; i++) {
        if (x >= foundation_x && x <= foundation_x + CARD_WIDTH &&
            y >= CARD_SPACING && y <= CARD_SPACING + CARD_HEIGHT) {
            return {2 + i, foundation_[i].empty() ? -1 : static_cast<int>(foundation_[i].size() - 1)};
        }
        foundation_x += CARD_WIDTH + CARD_SPACING;
    }

    // Check tableau piles - check from top card down
    int tableau_y = CARD_SPACING + CARD_HEIGHT + VERT_SPACING;
    for (int i = 0; i < 7; i++) {
        int pile_x = CARD_SPACING + i * (CARD_WIDTH + CARD_SPACING);
        if (x >= pile_x && x <= pile_x + CARD_WIDTH) {
            const auto& pile = tableau_[i];
            if (pile.empty() && y >= tableau_y && y <= tableau_y + CARD_HEIGHT) {
                return {6 + i, -1};
            }
            
            // Check cards from top to bottom
            for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
                int card_y = tableau_y + j * VERT_SPACING;
                if (y >= card_y && y <= card_y + CARD_HEIGHT) {
                    if (pile[j].face_up) {
                        return {6 + i, j};
                    }
                    break;  // Hit a face-down card, stop checking
                }
            }
        }
    }

    return {-1, -1};
}

bool SolitaireGame::canMoveToPile(
    const std::vector<cardlib::Card>& cards,
    const std::vector<cardlib::Card>& target) const {
    if (cards.empty())
        return false;

    const auto& moving_card = cards[0];

    // Moving to an empty pile
    if (target.empty()) {
        // For tableau: only allow kings on empty spaces
        return static_cast<int>(moving_card.rank) == static_cast<int>(cardlib::Rank::KING);
    }

    const auto& target_card = target.back();
    
    // Moving to tableau pile
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
