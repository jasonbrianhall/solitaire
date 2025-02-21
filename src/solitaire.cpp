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
    const auto &pile = foundation_[pile_index - 2];
    return !pile.empty() && static_cast<size_t>(card_index) == pile.size() - 1;
  }

  // Can drag from tableau if cards are face up
  if (pile_index >= 6 && pile_index <= 12) {
    const auto &pile = tableau_[pile_index - 6];
    return !pile.empty() && card_index >= 0 &&
           static_cast<size_t>(card_index) < pile.size();
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
    y = CARD_SPACING + CARD_HEIGHT + VERT_SPACING;
    for (size_t i = 0; i < game->tableau_.size(); i++) {
        x = CARD_SPACING + i * (CARD_WIDTH + CARD_SPACING);
        const auto& pile = game->tableau_[i];
        
        for (size_t j = 0; j < pile.size(); j++) {
            const auto& tableau_card = pile[j];
            if (tableau_card.face_up) {
                if (auto img = game->deck_.getCardImage(tableau_card.card)) {
                    game->drawCard(cr, x, y, &(*img));
                }
            } else {
                if (auto back_img = game->deck_.getCardBackImage()) {
                    game->drawCard(cr, x, y, &(*back_img));
                }
            }
            y += VERT_SPACING;
        }
    }

    return TRUE;
}

gboolean SolitaireGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                      gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (event->button == 1) { // Left click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    if (pile_index >= 0) {
      game->dragging_ = true;
      game->drag_source_pile_ = pile_index;
      game->drag_start_x_ = event->x;
      game->drag_start_y_ = event->y;

      auto &source_pile = game->getPileReference(pile_index);
      if (card_index >= 0 &&
          static_cast<size_t>(card_index) < source_pile.size()) {
        game->drag_cards_.assign(source_pile.begin() + card_index,
                                 source_pile.end());
      }
    }
  }

  return TRUE;
}

gboolean SolitaireGame::onButtonRelease(GtkWidget *widget,
                                        GdkEventButton *event, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (event->button == 1 && game->dragging_) {
    auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

    if (target_pile >= 0) {
      auto &target = game->getPileReference(target_pile);

      if (game->canMoveToPile(game->drag_cards_, target)) {
        auto &source = game->getPileReference(game->drag_source_pile_);
        source.erase(source.end() - game->drag_cards_.size(), source.end());

        target.insert(target.end(), game->drag_cards_.begin(),
                      game->drag_cards_.end());
      }
    }

    game->dragging_ = false;
    game->drag_cards_.clear();
    game->drag_source_pile_ = -1;

    gtk_widget_queue_draw(game->game_area_);

    if (game->checkWinCondition()) {
      GtkWidget *dialog = gtk_message_dialog_new(
          GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Congratulations! You've won!");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);

      game->initializeGame();
      gtk_widget_queue_draw(game->game_area_);
    }
  }

  return TRUE;
}

gboolean SolitaireGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                       gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (game->dragging_) {
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
    return {1, waste_.empty() ? -1 : 0};
  }

  // Check foundation piles
  int foundation_x = 3 * (CARD_WIDTH + CARD_SPACING);
  for (int i = 0; i < 4; i++) {
    if (x >= foundation_x && x <= foundation_x + CARD_WIDTH &&
        y >= CARD_SPACING && y <= CARD_SPACING + CARD_HEIGHT) {
      return {2 + i, foundation_[i].empty()
                         ? -1
                         : static_cast<int>(foundation_[i].size() - 1)};
    }
    foundation_x += CARD_WIDTH + CARD_SPACING;
  }

  // Check tableau piles
  int tableau_y = CARD_SPACING + CARD_HEIGHT + VERT_SPACING;
  for (int i = 0; i < 7; i++) {
    int pile_x = CARD_SPACING + i * (CARD_WIDTH + CARD_SPACING);
    if (x >= pile_x && x <= pile_x + CARD_WIDTH) {
      const auto &pile = tableau_[i];
      for (int j = 0; j < static_cast<int>(pile.size()); j++) {
        int card_y = tableau_y + j * VERT_SPACING;
        if (y >= card_y && y <= card_y + CARD_HEIGHT) {
          return {6 + i, j};
        }
      }
      if (pile.empty() && y >= tableau_y && y <= tableau_y + CARD_HEIGHT) {
        return {6 + i, -1};
      }
    }
  }

  return {-1, -1};
}

bool SolitaireGame::canMoveToPile(
    const std::vector<cardlib::Card> &cards,
    const std::vector<cardlib::Card> &target) const {
  if (cards.empty())
    return false;

  // Moving to foundation pile
  if (target.empty()) {
    return cards[0].rank == cardlib::Rank::ACE;
  }

  const auto &target_card = target.back();
  const auto &moving_card = cards[0];

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
