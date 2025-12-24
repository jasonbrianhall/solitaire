#include "freecell.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
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
      sounds_zip_path_("sound.zip"),
      current_seed_(0) {
  srand(time(NULL));  // Seed the random number generator with current time
  current_seed_ = rand();  // Generate random seed
  initializeGame();
  initializeSettingsDir();
  initializeAudio();
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
        if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
          // Initialize a single deck for Classic FreeCell
          deck_ = cardlib::Deck(path);
          deck_.removeJokers();
        } else {
          // Initialize a MultiDeck with 2 decks for Double FreeCell
          multi_deck_ = cardlib::MultiDeck(2, path);
          multi_deck_.includeJokersInAllDecks(false);
          has_multi_deck_ = true;
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

    // Shuffle with current seed
    if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
      deck_.shuffle(current_seed_);
    } else {
      multi_deck_.shuffle(current_seed_);
    }

    // Update layout based on game mode
    updateLayoutForGameMode();
    
    // Initialize the foundation cards tracking vector
    animated_foundation_cards_.clear();
    animated_foundation_cards_.resize(4);
    for (auto& pile : animated_foundation_cards_) {
      if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
        pile.resize(13, false);  // 13 cards per foundation in classic mode
      } else {
        pile.resize(26, false);  // 26 cards per foundation in double mode (2 full sequences)
      }
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

  // Reset freecells and foundation based on game mode
  updateLayoutForGameMode();

  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    // Deal to 8 tableau columns for Classic FreeCell (52 cards)
    int currentColumn = 0;
    
    // Deal all cards to the tableau columns
    while (auto card = deck_.drawCard()) {
      tableau_[currentColumn].push_back(*card);
      currentColumn = (currentColumn + 1) % 8;  // Cycle through 8 columns
    }
  } else {
    // Deal to 10 tableau columns for Double FreeCell (104 cards)
    // First four columns get 11 cards, remaining six get 10 cards
    for (int i = 0; i < 104; i++) {
      auto card = multi_deck_.drawCard();
      if (!card.has_value()) {
        break;  // No more cards
      }
      
      int column = i % 10;  // Determine which of the 10 columns
      tableau_[column].push_back(*card);
    }
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
      card.y += move_y - arc_offset * 0.4; // Apply a small amount of arc

      // Update rotation (gradually reduce to zero)
      card.rotation *= 0.95;

      all_cards_arrived = false;
    }
  }

  // Check if we're done dealing and all cards have arrived
  // Use the total number of cards based on game mode
  int total_cards = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 52 : 104;
  
  if (all_cards_arrived && cards_dealt_ >= total_cards) {
    completeDeal();
  }

  refreshDisplay();
}

void FreecellGame::dealNextCard() {
  int total_cards = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 52 : 104;
  int num_columns = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 8 : 10;
  
  if (cards_dealt_ >= total_cards)
    return;

  // Calculate which tableau column and position this card belongs to
  int column_index = cards_dealt_ % num_columns;
  int card_index = cards_dealt_ / num_columns;
  
  // For Double FreeCell, apply special distribution (first 4 columns get 11 cards, rest get 10)
  if (current_game_mode_ == GameMode::DOUBLE_FREECELL) {
    // We need to adjust the card_index calculation for Double FreeCell's distribution
    if (cards_dealt_ >= num_columns * 10) {
      // We're dealing the extra cards to the first 4 columns
      column_index = cards_dealt_ - (num_columns * 10);
      if (column_index >= 4) {
        // We've dealt all 104 cards
        cards_dealt_++;
        return;
      }
      card_index = 10; // These are the 11th cards in those columns
    }
  }

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
  
  // Add a unique identifier to help distinguish duplicate cards
  anim_card.source_pile = column_index;  // Store the column index
  anim_card.face_up = (card_index == card_index);  // Just a hack to store card_index (always true)
  
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

  //refreshDisplay();
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

  //=======================================================
  // GAME MENU - Core game functions
  //=======================================================
  GtkWidget *gameMenu = gtk_menu_new();
  GtkWidget *gameMenuItem = gtk_menu_item_new_with_mnemonic("_Game");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

  // Game control section
  GtkWidget *newGameItem = gtk_menu_item_new_with_mnemonic("_New Game (CTRL+N)");
  g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

  GtkWidget *restartGameItem = gtk_menu_item_new_with_mnemonic("_Restart Game (CTRL+R)");
  g_signal_connect(G_OBJECT(restartGameItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<FreecellGame *>(data)->restartGame();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), restartGameItem);

  // Separator
  GtkWidget *sep1 = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep1);

  // Auto-finish option
  GtkWidget *autoFinishItem = gtk_menu_item_new_with_mnemonic("Auto-_Finish Game (F)");
  g_signal_connect(G_OBJECT(autoFinishItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<FreecellGame *>(data)->autoFinishGame();
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), autoFinishItem);

  // Enter Seed option
  GtkWidget *seedItem = gtk_menu_item_new_with_label("Enter Seed...");
  g_signal_connect(G_OBJECT(seedItem), "activate", 
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    static_cast<FreecellGame *>(data)->promptForSeed();
                  }), 
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), seedItem);


  GtkWidget *modeMenuItem = gtk_menu_item_new_with_mnemonic("Game _Mode");
  GtkWidget *modeMenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(modeMenuItem), modeMenu);
  
  // Create radio items for game modes
  GSList *mode_group = NULL;
  classic_mode_item_ = gtk_radio_menu_item_new_with_label(mode_group, "Classic FreeCell");
  mode_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(classic_mode_item_));
  double_mode_item_ = gtk_radio_menu_item_new_with_label(mode_group, "Double FreeCell");
  
  // Set initial state
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(classic_mode_item_), 
      current_game_mode_ == GameMode::CLASSIC_FREECELL);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(double_mode_item_), 
      current_game_mode_ == GameMode::DOUBLE_FREECELL);
  
  // Connect signals
  g_signal_connect(G_OBJECT(classic_mode_item_), "toggled",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    FreecellGame *game = static_cast<FreecellGame *>(data);
                    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
                      game->setGameMode(GameMode::CLASSIC_FREECELL);
                    }
                  }),
                  this);
  
  g_signal_connect(G_OBJECT(double_mode_item_), "toggled",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    FreecellGame *game = static_cast<FreecellGame *>(data);
                    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
                      game->setGameMode(GameMode::DOUBLE_FREECELL);
                    }
                  }),
                  this);
  
  // Add radio items to the mode menu
  gtk_menu_shell_append(GTK_MENU_SHELL(modeMenu), classic_mode_item_);
  gtk_menu_shell_append(GTK_MENU_SHELL(modeMenu), double_mode_item_);
  
  // Add mode menu to game menu
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), modeMenuItem);

  // Add separator after mode menu
  GtkWidget *sep_mode = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep_mode);

  // Quit
  GtkWidget *quitItem = gtk_menu_item_new_with_mnemonic("_Quit (CTRL+Q)");
  g_signal_connect(G_OBJECT(quitItem), "activate", G_CALLBACK(onQuit), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), quitItem);

  // Add Game menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), gameMenuItem);

  //=======================================================
  // OPTIONS MENU - Visual and appearance options
  //=======================================================
  GtkWidget *optionsMenu = gtk_menu_new();
  GtkWidget *optionsMenuItem = gtk_menu_item_new_with_mnemonic("_Options");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(optionsMenuItem), optionsMenu);

  // Load custom deck
  GtkWidget *loadDeckItem = gtk_menu_item_new_with_mnemonic("_Load Deck (CTRL+L)");
  g_signal_connect(
      G_OBJECT(loadDeckItem), "activate",
      G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        FreecellGame *game = static_cast<FreecellGame *>(data);
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Load Custom Card Deck", GTK_WINDOW(game->window_),
            GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open", GTK_RESPONSE_ACCEPT, NULL);
        
        // Create file filter for ZIP files
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Card Deck Files (*.zip)");
        gtk_file_filter_add_pattern(filter, "*.zip");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
          char *filename =
              gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
          
          try {
            // Try to load the new deck
            game->deck_ = cardlib::Deck(filename);
            game->deck_.removeJokers();
            game->deck_.shuffle(game->current_seed_);
            
            // Reinitialize card cache with new deck
            game->initializeCardCache();
            
            // Restart the game with the new deck
            game->initializeGame();
            game->refreshDisplay();
            
            // Optional: Show success message
            GtkWidget *success_dialog = gtk_message_dialog_new(
                GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "Custom deck loaded successfully!");
            gtk_dialog_run(GTK_DIALOG(success_dialog));
            gtk_widget_destroy(success_dialog);
            
          } catch (const std::exception &e) {
            // Show error message if deck loading fails
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Failed to load deck: %s", e.what());
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
          }
          
          g_free(filename);
        }
        gtk_widget_destroy(dialog);
      }),
      this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), loadDeckItem);

  // Separator
  GtkWidget *sepOptions = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), sepOptions);
  
  // Visual options
  GtkWidget *fullscreenItem = gtk_menu_item_new_with_mnemonic("Toggle _Fullscreen (F11)");
  g_signal_connect(G_OBJECT(fullscreenItem), "activate", G_CALLBACK(onToggleFullscreen), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), fullscreenItem);

  // Sound toggle
  GtkWidget *soundItem = gtk_check_menu_item_new_with_mnemonic("Enable _Sound");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(soundItem), sound_enabled_);
  g_signal_connect(G_OBJECT(soundItem), "toggled",
                 G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                   FreecellGame *game = static_cast<FreecellGame *>(data);
                   game->sound_enabled_ = gtk_check_menu_item_get_active(
                       GTK_CHECK_MENU_ITEM(widget));
                 }),
                 this);
  gtk_menu_shell_append(GTK_MENU_SHELL(optionsMenu), soundItem);

  // Add Options menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), optionsMenuItem);

  //=======================================================
  // HELP MENU
  //=======================================================
  GtkWidget *helpMenu = gtk_menu_new();
  GtkWidget *helpMenuItem = gtk_menu_item_new_with_mnemonic("_Help");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMenuItem), helpMenu);

 GtkWidget *howtoplayItem = gtk_menu_item_new_with_mnemonic("_How to play");
  g_signal_connect(G_OBJECT(howtoplayItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    FreecellGame *game = static_cast<FreecellGame *>(data);
                    
                    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                        "How to Play", GTK_WINDOW(game->window_),
                        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                                   GTK_DIALOG_DESTROY_WITH_PARENT),
                        "OK", GTK_RESPONSE_OK, NULL);
                    
                    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);
                    
                    GtkWidget *label = gtk_label_new(NULL);
const char *markup =
    "How to Play FreeCell Solitaire\n\n"
    "GAME MODES:\n"
    "- Classic FreeCell: Play with one 52-card deck\n"
    "- Double FreeCell: Play with two 52-card decks (104 cards total)\n\n"
    "OBJECTIVE:\n"
    "Move all cards to the foundation piles, building up by suit from Ace to King.\n\n"
    "GAME SETUP:\n"
    "Classic FreeCell:\n"
    "- 52 cards dealt face-up across 8 tableau columns\n"
    "- 4 free cells for temporary storage\n"
    "- 4 foundation piles\n\n"
    "Double FreeCell:\n"
    "- 104 cards dealt face-up across 10 tableau columns\n"
    "- 6 free cells for temporary storage\n"
    "- 4 foundation piles (each holds 26 cards)\n\n"
    "RULES:\n"
    "1. Moving Cards in the Tableau:\n"
    "   - Cards can be moved one at a time or in valid sequences\n"
    "   - Cards must be placed in descending order with alternating colors\n"
    "   - Move multiple cards based on available free cells and empty columns\n"
    "   - Maximum movable cards = (empty free cells + 1) × 2^(empty columns)\n\n"
    "2. Free Cells:\n"
    "   - Classic FreeCell: 4 free cells\n"
    "   - Double FreeCell: 6 free cells\n"
    "   - Each cell can hold only one card\n"
    "   - Use free cells to temporarily store cards\n\n"
    "3. Foundation Piles:\n"
    "   - 4 foundation piles, one for each suit\n"
    "   - Build from Ace to King in ascending order\n"
    "   - Classic FreeCell: Each pile holds 13 cards\n"
    "   - Double FreeCell: Each pile holds 26 cards (two complete sequences)\n"
    "   - Once a card is placed in a foundation, it can be moved back (rarely useful)\n\n"
    "4. Empty Tableau Columns:\n"
    "   - Any card or valid card sequence can be placed in an empty column\n"
    "   - Empty columns are crucial for strategic card movement\n\n"
    "CONTROLS:\n"
    "- Left-click and drag to move cards\n"
    "- Right-click or Spacebar to auto-move cards to foundations\n"
    "- Arrow keys to navigate between piles\n"
    "- Enter to select/place cards\n"
    "- 'F' to auto-finish the game\n\n"
    "STRATEGY TIPS:\n"
    "1. Create empty columns early in the game\n"
    "2. Move Aces and low cards to foundations when safe\n"
    "3. Avoid blocking higher cards with same-color cards\n"
    "4. Use free cells sparingly\n"
    "5. Plan moves carefully - sometimes delay moving to foundations\n\n"
    "WINNING THE GAME:\n"
    "- Classic FreeCell: Move all 52 cards to foundations\n"
    "- Double FreeCell: Move all 104 cards to foundations";
                    
                    gtk_label_set_markup(GTK_LABEL(label), markup);
                    gtk_container_add(GTK_CONTAINER(content_area), label);
                    gtk_widget_show_all(dialog);
                    
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                  }),
                  this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), howtoplayItem);

  // Keyboard shortcuts item
  GtkWidget *shortcutsItem = gtk_menu_item_new_with_mnemonic("_Keyboard Shortcuts");
  g_signal_connect(G_OBJECT(shortcutsItem), "activate",
                  G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                    FreecellGame *game = static_cast<FreecellGame *>(data);
                    
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
                        "<b>Ctrl+R</b> - Restart Game\n"
                        "<b>Ctrl+L</b> - Load Custom Deck\n"
                        "<b>Ctrl+Q</b> - Quit\n"
                        "<b>Ctrl+H</b> - Help\n"
                        "<b>Arrow Keys</b> - Navigate piles\n"
                        "<b>Enter</b> - Select or place cards\n"
                        "<b>Esc</b> - Cancel selection\n"
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
  GtkWidget *aboutItem = gtk_menu_item_new_with_mnemonic("_About (CTRL+H)");
  g_signal_connect(G_OBJECT(aboutItem), "activate", G_CALLBACK(onAbout), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutItem);

  // Add Help menu to menubar
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMenuItem);

  // Show all menu items
  gtk_widget_show_all(menubar);
}

void FreecellGame::onNewGame(GtkWidget *widget, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  
  // Check if win animation is active
  if (game->win_animation_active_) {
    game->stopWinAnimation();
  }

  game->current_seed_ = rand();
  game->initializeGame();
  game->refreshDisplay();
}

void FreecellGame::restartGame() {
  // Check if win animation is active
  if (win_animation_active_) {
    stopWinAnimation();
  }

  // Keep the current seed and restart the game
  initializeGame();
  refreshDisplay();
}

void FreecellGame::promptForSeed() {
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

  // Add program information in a scrolled window
  GtkWidget *instructions_text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(instructions_text), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(instructions_text), FALSE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(instructions_text), 12);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(instructions_text), 12);

  const char *about_text =
      "Freecell Solitaire\n\n"
      "A classic card game that combines strategy, patience, and skill. "
      "This implementation provides both Classic and Double FreeCell modes, "
      "offering players a challenging and engaging solitaire experience.\n\n"
      "Features:\n"
      "- Classic and Double FreeCell game modes\n"
      "- Customizable card decks\n"
      "- Smooth, animated card movements\n"
      "- Keyboard and mouse support\n"
      "- Sound effects\n\n"
      "Developed as an open-source project to provide an enjoyable "
      "and accessible solitaire gaming experience.\n\n"
      "Software Design:\n"
      "- C++ Programming\n"
      "- GTK+ GUI Framework\n"
      "- Cairo Graphics Library\n\n"
      "Created with passion for game development and software craftsmanship.\n\n"
      "Licensed under the MIT License\n"
      "Copyright © 2025 Jason Hall\n"
      "https://github.com/jasonbrianhall/solitaire";

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(instructions_text));
  gtk_text_buffer_set_text(buffer, about_text, -1);

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

void FreecellGame::initializeCardCache() {
  // Get display scale factor
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
  
  // Calculate actual pixel dimensions needed for the surface
  // (Cairo surfaces need physical pixels, not logical pixels)
  int surface_width = static_cast<int>(current_card_width_ * display_scale);
  int surface_height = static_cast<int>(current_card_height_ * display_scale);
  
  // Pre-load all card images into cairo surfaces with current dimensions
  cleanupCardCache();
  for (const auto &card : deck_.getAllCards()) {
    if (auto img = deck_.getCardImage(card)) {
      GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
      gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(), nullptr);
      gdk_pixbuf_loader_close(loader, nullptr);
      GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
          pixbuf, surface_width, surface_height, GDK_INTERP_BILINEAR);
      cairo_surface_t *surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, surface_width, surface_height);
      
      // Set the device scale on the surface so Cairo knows about the scaling
      cairo_surface_set_device_scale(surface, display_scale, display_scale);
      
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

void FreecellGame::refreshDisplay() {
  if (game_area_) {
    gtk_widget_queue_draw(game_area_);
  }
}

void FreecellGame::setupEasyGame() {
  // Clear all piles
  freecells_.clear();
  foundation_.clear();
  tableau_.clear();

  // Determine the number of freecells and tableau columns based on game mode
  int num_freecells = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  int num_tableau = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 8 : 10;

  // Initialize freecells (4 or 6 empty cells depending on mode)
  freecells_.resize(num_freecells);
  
  // Initialize foundation piles (4 empty piles for aces)
  foundation_.resize(4);

  // Initialize tableau (8 or 10 piles depending on mode)
  tableau_.resize(num_tableau);

  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    // ==================== CLASSIC FREECELL MODE ====================
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
  }
  else {
    // ==================== DOUBLE FREECELL MODE ====================
    // In Double FreeCell, we need to arrange 104 cards (2 complete decks)
    // Each foundation pile will need to contain 26 cards (2 complete sequences)
    
    // Create the cards with "alternate art" flag to distinguish the second deck
    bool first_deck = false;  // First deck uses regular art
    bool second_deck = true;  // Second deck uses alternate art
    
    // First Deck
    // Hearts (K to A) in column 0
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::KING, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::QUEEN, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::JACK, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TEN, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::NINE, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::EIGHT, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SEVEN, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SIX, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FIVE, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FOUR, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::THREE, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TWO, first_deck});
    tableau_[0].push_back({cardlib::Suit::HEARTS, cardlib::Rank::ACE, first_deck});
    
    // Diamonds (K to A) in column 1
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::KING, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::QUEEN, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::JACK, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TEN, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::NINE, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::EIGHT, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SEVEN, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SIX, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FIVE, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FOUR, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::THREE, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TWO, first_deck});
    tableau_[1].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::ACE, first_deck});
    
    // Clubs (K to A) in column 2
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::KING, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::QUEEN, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::JACK, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TEN, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::NINE, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::EIGHT, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SEVEN, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SIX, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FIVE, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FOUR, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::THREE, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TWO, first_deck});
    tableau_[2].push_back({cardlib::Suit::CLUBS, cardlib::Rank::ACE, first_deck});
    
    // Spades (K to A) in column 3
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::KING, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::QUEEN, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::JACK, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::TEN, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::NINE, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::EIGHT, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::SEVEN, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::SIX, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::FIVE, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::FOUR, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::THREE, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::TWO, first_deck});
    tableau_[3].push_back({cardlib::Suit::SPADES, cardlib::Rank::ACE, first_deck});
    
    // Second Deck
    // Hearts (K to A) in column 4
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::KING, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::QUEEN, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::JACK, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TEN, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::NINE, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::EIGHT, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SEVEN, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::SIX, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FIVE, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::FOUR, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::THREE, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::TWO, second_deck});
    tableau_[4].push_back({cardlib::Suit::HEARTS, cardlib::Rank::ACE, second_deck});
    
    // Diamonds (K to A) in column 5
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::KING, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::QUEEN, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::JACK, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TEN, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::NINE, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::EIGHT, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SEVEN, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::SIX, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FIVE, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::FOUR, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::THREE, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::TWO, second_deck});
    tableau_[5].push_back({cardlib::Suit::DIAMONDS, cardlib::Rank::ACE, second_deck});
    
    // Clubs (K to A) in column 6
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::KING, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::QUEEN, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::JACK, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TEN, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::NINE, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::EIGHT, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SEVEN, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::SIX, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FIVE, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::FOUR, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::THREE, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::TWO, second_deck});
    tableau_[6].push_back({cardlib::Suit::CLUBS, cardlib::Rank::ACE, second_deck});
    
    // Spades (K to A) in column 7
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::KING, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::QUEEN, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::JACK, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::TEN, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::NINE, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::EIGHT, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::SEVEN, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::SIX, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::FIVE, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::FOUR, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::THREE, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::TWO, second_deck});
    tableau_[7].push_back({cardlib::Suit::SPADES, cardlib::Rank::ACE, second_deck});
    
    // We'll leave the 6 freecells empty for this easy game
  }
  
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

bool FreecellGame::canMoveTableauStack(const std::vector<cardlib::Card>& cards, int tableau_idx) {
    // Just delegate to the const version to avoid duplicating code
    return const_cast<const FreecellGame*>(this)->canMoveTableauStack(cards, tableau_idx);
}

// Check if a stack of cards can be moved to a tableau pile
bool FreecellGame::canMoveTableauStack(const std::vector<cardlib::Card>& cards, int tableau_idx) const {
  // Tableau must be within range
  if (tableau_idx < 0 || static_cast<size_t>(tableau_idx) >= tableau_.size()) {
    return false;
  }
  
  // First make sure the card sequence itself is valid
  if (!isValidTableauSequence(cards)) {
    return false;
  }
  
  // If destination tableau isn't empty, check if we can place the bottom card
  if (!tableau_[tableau_idx].empty()) {
    const cardlib::Card& bottom_card = cards[0];
    const cardlib::Card& top_card = tableau_[tableau_idx].back();
    
    // Bottom card must be of different color and one rank lower than tableau's top card
    bool different_colors = isCardRed(bottom_card) != isCardRed(top_card);
    bool descending_rank = static_cast<int>(bottom_card.rank) + 1 == static_cast<int>(top_card.rank);
    
    if (!different_colors || !descending_rank) {
      return false;
    }
  }
  
  // If we're only moving one card, we can always do that
  if (cards.size() == 1) {
    return true;
  }
  
  // For multiple cards, we need to check the Freecell formula
  
  // Count empty free cells (number depends on game mode)
  int empty_freecells = 0;
  for (const auto& cell : freecells_) {
    if (!cell.has_value()) {
      empty_freecells++;
    }
  }
  
  // Count empty tableau columns (excluding the destination)
  int empty_tableau_columns = 0;
  for (int i = 0; i < tableau_.size(); i++) {
    if (i != tableau_idx && tableau_[i].empty()) {
      empty_tableau_columns++;
    }
  }
  
  // Calculate max movable cards based on empty free cells and empty columns
  // The formula is: (empty_freecells + 1) * 2^(empty_tableau_columns)
  int max_movable_cards = (empty_freecells + 1) * (1 << empty_tableau_columns);
  
  return cards.size() <= max_movable_cards;
}

void FreecellGame::updateLayoutForGameMode() {
  // Clear existing piles
  freecells_.clear();
  foundation_.clear();
  tableau_.clear();
  
  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    // Classic FreeCell: 4 freecells, 4 foundation piles, 8 tableau columns
    freecells_.resize(4);
    foundation_.resize(4);
    tableau_.resize(8);
  } else {
    // Double FreeCell: 6 freecells, 4 foundation piles (will hold 2 sets each), 10 tableau columns
    freecells_.resize(6);
    foundation_.resize(4);
    tableau_.resize(10);
  }
  
  // Set minimum size based on game mode
  if (game_area_) {
    if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
      gtk_widget_set_size_request(
          game_area_,
          BASE_CARD_WIDTH * 8 + BASE_CARD_SPACING * 9, // 8 columns + spacing
          BASE_CARD_HEIGHT * 2 + BASE_VERT_SPACING * 7 // 2 rows + tableau
      );
    } else {
      gtk_widget_set_size_request(
          game_area_,
          BASE_CARD_WIDTH * 10 + BASE_CARD_SPACING * 11, // 10 columns + spacing
          BASE_CARD_HEIGHT * 2 + BASE_VERT_SPACING * 10  // 2 rows + tableau
      );
    }
  }
}

void FreecellGame::setGameMode(GameMode mode) {
  if (mode == current_game_mode_) {
    return;  // No change
  }
  
  // Update mode
  GameMode previous_mode = current_game_mode_;
  
  // Confirm with user before changing game in progress
  bool start_new_game = true;
  if (!tableau_.empty() && !tableau_[0].empty()) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Changing game mode will start a new game. Continue?");
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    start_new_game = (response == GTK_RESPONSE_YES);
    gtk_widget_destroy(dialog);
    
    // If user cancelled, don't change the mode
    if (!start_new_game) {
      // Restore the radio button state to match the current mode
      if (previous_mode == GameMode::CLASSIC_FREECELL) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(classic_mode_item_), TRUE);
      } else {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(double_mode_item_), TRUE);
      }
      return;  // Exit without changing mode
    }
  }
  
  // Only update the actual mode if we're proceeding with the change
  current_game_mode_ = mode;
  
  if (start_new_game) {
    // Initialize a new game with the updated mode
    initializeGame();
  }
}
