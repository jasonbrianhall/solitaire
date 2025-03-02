#include "freecell.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

FreecellGame::FreecellGame()
    : dragging_(false), drag_source_pile_(-1),
      window_(nullptr), game_area_(nullptr), buffer_surface_(nullptr),
      buffer_cr_(nullptr), 
      current_card_width_(BASE_CARD_WIDTH),
      current_card_height_(BASE_CARD_HEIGHT),
      current_card_spacing_(BASE_CARD_SPACING),
      current_vert_spacing_(BASE_VERT_SPACING), is_fullscreen_(false),
      selected_pile_(-1), selected_card_idx_(-1),
      keyboard_navigation_active_(false), keyboard_selection_active_(false),
      source_pile_(-1), source_card_idx_(-1),
      drag_source_card_idx_(-1),
      sound_enabled_(true),      
      sounds_zip_path_("sound.zip") {
  initializeGame();
  initializeSettingsDir();
  initializeAudio();
  loadSettings();
}

FreecellGame::~FreecellGame() {
  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
  }
  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
  }
  cleanupAudio();
}

void FreecellGame::run(int argc, char **argv) {
  gtk_init(&argc, &argv);
  setupWindow();
  setupGameArea();
  gtk_main();
}

void FreecellGame::initializeGame() {
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

    deck_.shuffle();

    // Clear all piles
    freecells_.clear();
    foundation_.clear();
    tableau_.clear();

    // Initialize freecells (4 empty cells)
    freecells_.resize(4);
    
    // Initialize foundation piles (4 empty piles for aces)
    foundation_.resize(4);

    // Initialize tableau (8 piles for Freecell)
    tableau_.resize(8);
    
    // Initialize the foundation cards tracking vector
    animated_foundation_cards_.clear();
    animated_foundation_cards_.resize(4);
    for (auto& pile : animated_foundation_cards_) {
      pile.resize(13, false);  // Each foundation can have at most 13 cards (A to K)
    }

    // Deal cards
    deal();

  } catch (const std::exception &e) {
    std::cerr << "Fatal error during game initialization: " << e.what()
              << std::endl;
    exit(1);
  }
}

void FreecellGame::deal() {
  // Clear all piles first
  freecells_.clear();
  foundation_.clear();
  tableau_.clear();

  // Reset freecells and foundation
  freecells_.resize(4);
  foundation_.resize(4);
  tableau_.resize(8);

  // Deal to tableau columns (all face up)
  // In Freecell, all 52 cards are dealt across 8 columns
  int currentColumn = 0;
  
  // Deal all cards to the tableau columns
  while (auto card = deck_.drawCard()) {
    tableau_[currentColumn].push_back(*card);
    currentColumn = (currentColumn + 1) % 8;  // Move to next column in round-robin fashion
  }

  // Start the deal animation
  startDealAnimation();
}

void FreecellGame::startDealAnimation() {
  if (deal_animation_active_)
    return;

  deal_animation_active_ = true;
  cards_dealt_ = 0;
  deal_timer_ = 0;
  deal_cards_.clear();

  // Make sure we're not using the same timer ID as the win animation
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  // Set up a new animation timer
  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onDealAnimationTick, this);

  // Deal the first card immediately
  dealNextCard();

  // Force a redraw to ensure we don't see the cards already in place
  refreshDisplay();
}

gboolean FreecellGame::onDealAnimationTick(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->updateDealAnimation();
  return game->deal_animation_active_ ? TRUE : FALSE;
}

void FreecellGame::updateDealAnimation() {
  if (!deal_animation_active_)
    return;

  // Launch new cards periodically
  deal_timer_ += ANIMATION_INTERVAL;
  if (deal_timer_ >= DEAL_INTERVAL) {
    deal_timer_ = 0;
    dealNextCard();
  }

  // Update all cards in animation
  bool all_cards_arrived = true;

  for (auto &card : deal_cards_) {
    if (!card.active)
      continue;

    // Calculate distance to target
    double dx = card.target_x - card.x;
    double dy = card.target_y - card.y;
    double distance = sqrt(dx * dx + dy * dy);

    if (distance < 5.0) {
      // Card has arrived at destination
      card.x = card.target_x;
      card.y = card.target_y;
      card.active = false;
    } else {
      // Move card toward destination with a more pronounced arc
      double speed = distance * 0.15 * DEAL_SPEED;
      double move_x = dx * speed / distance;
      double move_y = dy * speed / distance;

      // Add a slight arc to the motion (card rises then falls)
      double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
      double arc_height = 50.0; // Maximum height of the arc in pixels
      double arc_offset = sin(progress * G_PI) * arc_height;

      card.x += move_x;
      card.y += move_y - arc_offset * 0.1; // Apply a small amount of arc

      // Update rotation (gradually reduce to zero)
      card.rotation *= 0.95;

      all_cards_arrived = false;
    }
  }

  // Check if we're done dealing and all cards have arrived
  if (all_cards_arrived && cards_dealt_ >= 52) { // 52 cards in Freecell
    completeDeal();
  }

  refreshDisplay();
}

void FreecellGame::dealNextCard() {
  if (cards_dealt_ >= 52) // All cards are dealt in Freecell
    return;

  // Calculate which tableau column and position this card belongs to
  int column_index = cards_dealt_ % 8;
  int card_index = cards_dealt_ / 8;

  // Make sure the tableau column exists and has enough cards
  if (column_index >= tableau_.size() || card_index >= tableau_[column_index].size()) {
    // Skip this card (shouldn't happen in normal play)
    cards_dealt_++;
    return;
  }

  // Start position (from the center/top)
  double start_x = current_card_width_ * 4 + current_card_spacing_ * 4;
  double start_y = 0;

  // Calculate target position in the tableau
  double target_x = current_card_spacing_ + column_index * (current_card_width_ + current_card_spacing_);
  double target_y = (2 * current_card_spacing_ + current_card_height_) + card_index * current_vert_spacing_;

  // Create animation card
  AnimatedCard anim_card;
  anim_card.card = tableau_[column_index][card_index];
  anim_card.x = start_x;
  anim_card.y = start_y;
  anim_card.target_x = target_x;
  anim_card.target_y = target_y;
  anim_card.velocity_x = 0;
  anim_card.velocity_y = 0;
  anim_card.rotation = (rand() % 628) / 100.0 - 3.14; // Random initial rotation
  anim_card.rotation_velocity = 0;
  anim_card.active = true;
  anim_card.exploded = false;

  // Add to animation list
  deal_cards_.push_back(anim_card);
  cards_dealt_++;

  // Play deal sound
  playSound(GameSoundEvent::DealCard);
}

void FreecellGame::completeDeal() {
  deal_animation_active_ = false;

  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  deal_cards_.clear();
  cards_dealt_ = 0;

  refreshDisplay();
}

void FreecellGame::drawCard(cairo_t *cr, int x, int y, const cardlib::Card *card) {
  if (card) {
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
    // Draw an empty placeholder
    drawEmptyPile(cr, x, y);
  }
}

void FreecellGame::drawEmptyPile(cairo_t *cr, int x, int y) {
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

void FreecellGame::drawAnimatedCard(cairo_t *cr, const AnimatedCard &anim_card) {
  if (!anim_card.active)
    return;

  // Draw the card with rotation
  cairo_save(cr);

  // Move to card center for rotation
  cairo_translate(cr, anim_card.x + current_card_width_ / 2,
                  anim_card.y + current_card_height_ / 2);
  cairo_rotate(cr, anim_card.rotation);
  cairo_translate(cr, -current_card_width_ / 2, -current_card_height_ / 2);

  // Draw the card
  drawCard(cr, 0, 0, &anim_card.card);

  cairo_restore(cr);
}

void FreecellGame::setupWindow() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Freecell");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit), NULL);

  gtk_widget_add_events(window_, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window_), "key-press-event", G_CALLBACK(onKeyPress), this);

  // Create vertical box
  vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window_), vbox_);

  // Setup menu bar
  setupMenuBar();
}

void FreecellGame::setupGameArea() {
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
  g_signal_connect(G_OBJECT(game_area_), "button-press-event", G_CALLBACK(onButtonPress), this);
  g_signal_connect(G_OBJECT(game_area_), "button-release-event", G_CALLBACK(onButtonRelease), this);
  g_signal_connect(G_OBJECT(game_area_), "motion-notify-event", G_CALLBACK(onMotionNotify), this);

  // Add size-allocate signal handler for resize events
  g_signal_connect(G_OBJECT(game_area_), "size-allocate",
    G_CALLBACK(+[](GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
      FreecellGame *game = static_cast<FreecellGame *>(data);
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

  // Set minimum size
  gtk_widget_set_size_request(
      game_area_,
      BASE_CARD_WIDTH * 8 + BASE_CARD_SPACING * 9, // Minimum width for 8 cards + spacing
      BASE_CARD_HEIGHT * 2 + BASE_VERT_SPACING * 7 // Minimum height for 2 rows + tableau
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

void FreecellGame::setupMenuBar() {
  GtkWidget *menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(vbox_), menubar, FALSE, FALSE, 0);

  // Game menu
  GtkWidget *gameMenu = gtk_menu_new();
  GtkWidget *gameMenuItem = gtk_menu_item_new_with_mnemonic("_Game");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

  // New Game
  GtkWidget *newGameItem = gtk_menu_item_new_with_mnemonic("_New Game (CTRL+N)");
  g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

  // Fullscreen option
  GtkWidget *fullscreenItem = gtk_menu_item_new_with_mnemonic("Toggle _Fullscreen (F11)");
  g_signal_connect(G_OBJECT(fullscreenItem), "activate", G_CALLBACK(onToggleFullscreen), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), fullscreenItem);

  // Separator
  GtkWidget *sep = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep);

  GtkWidget *soundItem = gtk_check_menu_item_new_with_mnemonic("_Sound");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(soundItem), sound_enabled_);
  g_signal_connect(G_OBJECT(soundItem), "toggled",
                 G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                   FreecellGame *game = static_cast<FreecellGame *>(data);
                   game->sound_enabled_ = gtk_check_menu_item_get_active(
                       GTK_CHECK_MENU_ITEM(widget));
                 }),
                 this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), soundItem);

  // Add a separator before Quit item
  GtkWidget *sep2 = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep2);

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

void FreecellGame::onNewGame(GtkWidget *widget, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  
  // Check if win animation is active
  if (game->win_animation_active_) {
    game->stopWinAnimation();
  }

  game->initializeGame();
  game->refreshDisplay();
}

void FreecellGame::onQuit(GtkWidget *widget, gpointer data) {
  gtk_main_quit();
}

void FreecellGame::onAbout(GtkWidget * /* widget */, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  // Create dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "About Freecell", GTK_WINDOW(game->window_),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      "OK", GTK_RESPONSE_OK, NULL);

  // Set minimum dialog size
  gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

  // Create and configure the content area
  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content_area), 24);
  gtk_widget_set_margin_bottom(content_area, 12);

  // Add program name with larger font
  GtkWidget *name_label = gtk_label_new(NULL);
  const char *name_markup = "<span size='x-large' weight='bold'>Freecell</span>";
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

  const char *instructions =
      "How to Play Freecell:\n\n"
      "Objective:\n"
      "Build four foundation piles in ascending order by suit (A-K).\n\n"
      "Game Setup:\n"
      "- 52 cards are dealt face up to eight tableau columns\n"
      "- Four free cells are available for temporary card storage\n"
      "- Four foundation piles need to be built in ascending order by suit\n\n"
      "Rules:\n"
      "1. Only one card can be moved at a time unless empty columns are available\n"
      "2. In the tableau, cards must be placed in descending order with alternating colors\n"
      "3. Free cells can hold only one card each\n"
      "4. Foundation piles are built up by suit from Ace to King\n"
      "5. Empty tableau columns can be filled with any card\n\n"
      "Controls:\n"
      "- Left-click and drag to move cards\n"
      "- Right-click to automatically move cards to foundation piles when possible\n\n"
      "Keyboard Controls:\n"
      "- Arrow keys (←, →, ↑, ↓) to navigate between piles and cards\n"
      "- Enter to select a card or perform a move\n"
      "- F11 to toggle fullscreen mode\n"
      "- Ctrl+N for a new game\n"
      "- Ctrl+Q to quit\n"
      "- Ctrl+H for help\n\n"
      "Strategy:\n"
      "- Try to empty columns when possible to create more space for maneuvering\n"
      "- Plan ahead to uncover cards in a specific order\n"
      "- Use free cells wisely as a temporary storage\n\n";

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(instructions_text));
  gtk_text_buffer_set_text(buffer, instructions, -1);

  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  // Set the size of the scrolled window
  gtk_widget_set_size_request(scrolled_window, 550, 400);

  gtk_container_add(GTK_CONTAINER(scrolled_window), instructions_text);
  gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

  // Show all widgets before running the dialog
  gtk_widget_show_all(dialog);

  // Run dialog and check result
  gint result = gtk_dialog_run(GTK_DIALOG(dialog));
  
  // Check for Ctrl key when OK button is pressed
  if (result == GTK_RESPONSE_OK) {
    GdkModifierType modifiers;
    gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(dialog)), NULL, NULL, &modifiers);
    
    if (modifiers & GDK_CONTROL_MASK) {
      // Close dialog first
      gtk_widget_destroy(dialog);
      
      // Activate easter egg with easy game
      game->setupEasyGame();
      return;
    }
  }

  gtk_widget_destroy(dialog);
}

void FreecellGame::toggleFullscreen() {
  if (is_fullscreen_) {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = true;
  }
}

void FreecellGame::onToggleFullscreen(GtkWidget *widget, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->toggleFullscreen();
}

void FreecellGame::updateCardDimensions(int window_width, int window_height) {
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

  // Make sure cards don't overlap too much
  if (current_vert_spacing_ < current_card_height_ / 4) {
    current_vert_spacing_ = current_card_height_ / 4;
  }

  // Reinitialize card cache with new dimensions
  initializeCardCache();
}

double FreecellGame::getScaleFactor(int window_width, int window_height) const {
  // Calculate scale factors for both dimensions
  double width_scale = static_cast<double>(window_width) / BASE_WINDOW_WIDTH;
  double height_scale = static_cast<double>(window_height) / BASE_WINDOW_HEIGHT;

  // Use the smaller scale to ensure everything fits
  return std::min(width_scale, height_scale);
}

void FreecellGame::initializeCardCache() {
  // Pre-load all card images into cairo surfaces with current dimensions
  cleanupCardCache();

  for (const auto &card : deck_.getAllCards()) {
    if (auto img = deck_.getCardImage(card)) {
      GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
      gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(), nullptr);
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

      std::string key = std::to_string(static_cast<int>(card.suit)) +
                        std::to_string(static_cast<int>(card.rank));
      card_surface_cache_[key] = surface;

      g_object_unref(scaled);
      g_object_unref(loader);
    }
  }
}

void FreecellGame::cleanupCardCache() {
  for (auto &[key, surface] : card_surface_cache_) {
    if (surface) {
      cairo_surface_destroy(surface);
    }
  }
  card_surface_cache_.clear();
}

void FreecellGame::initializeSettingsDir() {
  const char *home_dir = nullptr;
  std::string app_dir;

#ifdef _WIN32
  home_dir = getenv("USERPROFILE");
  if (home_dir) {
    app_dir = std::string(home_dir) + "\\AppData\\Local\\Freecell";
  }
#else
  home_dir = getenv("HOME");
  if (home_dir) {
    app_dir = std::string(home_dir) + "/.config/freecell";
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

bool FreecellGame::loadSettings() {
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

  std::ifstream file(settings_file);
  if (!file) {
    std::cerr << "Failed to open settings file" << std::endl;
    return false;
  }

  // Read settings from file (can be extended as needed)
  std::string line;
  while (std::getline(file, line)) {
    // Process settings here
  }

  return true;
}

void FreecellGame::saveSettings() {
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

  // Write settings to file (can be extended as needed)
}

void FreecellGame::refreshDisplay() {
  if (game_area_) {
    gtk_widget_queue_draw(game_area_);
  }
}

// This is a simplified version of the sound setup from SolitaireGame
bool FreecellGame::initializeAudio() {
  // If sound is disabled, return early
  if (!sound_enabled_) {
    return false;
  }

  // Basic placeholder for audio initialization
  return true;
}

void FreecellGame::playSound(GameSoundEvent event) {
  // No-op implementation for now
  // Would use AudioManager to play sounds
}

void FreecellGame::cleanupAudio() {
  // No-op implementation for now
  // Would clean up AudioManager resources
}

bool FreecellGame::loadSoundFromZip(GameSoundEvent event, const std::string &soundFileName) {
  // Placeholder implementation
  return true;
}

bool FreecellGame::extractFileFromZip(const std::string &zipFilePath,
                                     const std::string &fileName,
                                     std::vector<uint8_t> &fileData) {
  // Placeholder implementation
  return true;
}

bool FreecellGame::setSoundsZipPath(const std::string &path) {
  sounds_zip_path_ = path;
  return true;
}

void FreecellGame::setupEasyGame() {
  // Clear all piles
  freecells_.clear();
  foundation_.clear();
  tableau_.clear();

  // Initialize freecells (4 empty cells)
  freecells_.resize(4);
  
  // Initialize foundation piles (4 empty piles for aces)
  foundation_.resize(4);

  // Initialize tableau (8 piles for Freecell)
  tableau_.resize(8);

  // Create a pre-arranged deck that's easy to solve
  // Distribute cards across all 8 columns, arranging by suit and rank
  
  // Hearts (K to A) in columns 0 and 1
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::KING});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::QUEEN});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::JACK});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TEN});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::NINE});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::EIGHT});
  tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SEVEN});
  
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SIX});
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FIVE});
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FOUR});
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::THREE});
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TWO});
  tableau_[1].push_back({cardlib::Suit::HEARTS, cardlib::Rank::ACE});
  
  // Diamonds (K to A) in columns 2 and 3
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::KING});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::QUEEN});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::JACK});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TEN});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::NINE});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::EIGHT});
  tableau_[2].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SEVEN});
  
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SIX});
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FIVE});
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FOUR});
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::THREE});
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TWO});
  tableau_[3].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::ACE});
  
  // Clubs (K to A) in columns 4 and 5
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::KING});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::QUEEN});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::JACK});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TEN});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::NINE});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::EIGHT});
  tableau_[4].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SEVEN});
  
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SIX});
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FIVE});
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FOUR});
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::THREE});
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TWO});
  tableau_[5].push_back({cardlib::Suit::CLUBS, cardlib::Rank::ACE});
  
  // Spades (K to A) in columns 6 and 7
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::KING});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::QUEEN});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::JACK});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::TEN});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::NINE});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::EIGHT});
  tableau_[6].push_back({cardlib::Suit::SPADES, cardlib::Rank::SEVEN});
  
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::SIX});
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::FIVE});
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::FOUR});
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::THREE});
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::TWO});
  tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::ACE});
  
  // Refresh the display
  refreshDisplay();
}

bool FreecellGame::autoFinishMoves() {
  bool moved_any = false;
  bool continue_checking = true;
  
  // Keep looking for moves until we can't find any more
  while (continue_checking) {
    continue_checking = false;
    
    // First check freecells for cards that can move to foundation
    for (int i = 0; i < freecells_.size(); i++) {
      if (!freecells_[i].has_value()) {
        continue;
      }
      
      const cardlib::Card& card = freecells_[i].value();
      
      // Try to move to foundation
      for (int foundation_idx = 0; foundation_idx < foundation_.size(); foundation_idx++) {
        if (canMoveToFoundation(card, foundation_idx)) {
          // Move card to foundation
          foundation_[foundation_idx].push_back(card);
          freecells_[i] = std::nullopt;
          moved_any = true;
          continue_checking = true;
          
          // Play card movement sound
          playSound(GameSoundEvent::CardPlace);
          break;
        }
      }
    }
    
    // Then check tableau piles for cards that can move to foundation
    for (int i = 0; i < tableau_.size(); i++) {
      if (tableau_[i].empty()) {
        continue;
      }
      
      const cardlib::Card& card = tableau_[i].back();
      
      // Try to move to foundation
      for (int foundation_idx = 0; foundation_idx < foundation_.size(); foundation_idx++) {
        if (canMoveToFoundation(card, foundation_idx)) {
          // Move card to foundation
          foundation_[foundation_idx].push_back(card);
          tableau_[i].pop_back();
          moved_any = true;
          continue_checking = true;
          
          // Play card movement sound
          playSound(GameSoundEvent::CardPlace);
          break;
        }
      }
    }
  }
  
  // Check if all cards are in the foundation (win condition)
  if (checkWinCondition()) {
    startWinAnimation();
  }
  
  return moved_any;
}

// Define main function to run the game
int main(int argc, char **argv) {
  FreecellGame game;
  game.run(argc, argv);
  return 0;
}


