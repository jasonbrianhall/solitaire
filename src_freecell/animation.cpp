#include "freecell.h"
#include <gtk/gtk.h>

#define GRAVITY 0.3
#define EXPLOSION_THRESHOLD_MIN 0.35
#define EXPLOSION_THRESHOLD_MAX 0.7
gboolean FreecellGame::onAnimationTick(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
}

void FreecellGame::updateWinAnimation() {
  if (!win_animation_active_)
    return;

  // Launch new cards periodically
  launch_timer_ += ANIMATION_INTERVAL;
  if (launch_timer_ >= 100) { // Launch a new card every 100ms
    launch_timer_ = 0;
    if (rand() % 100 < 10) {
        // Launch multiple cards in rapid succession
        for (int i = 0; i < 4; i++) {
            // Alternate between foundation and freecell launches
            if (i % 2 == 0) {
                launchNextCard();        // Launch from foundation
            } else {
                launchCardFromFreecell(); // Launch from freecell area
            }
        }
    } else {    
       // Randomly choose launch source
       if (rand() % 2 == 0) {
           launchNextCard();          // Launch from foundation
       } else {
           launchCardFromFreecell();  // Launch from freecell area
       }
    }
  }

  // Update physics for all active cards
  bool all_cards_finished = true;
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);

  const double explosion_min = allocation.height * EXPLOSION_THRESHOLD_MIN;
  const double explosion_max = allocation.height * EXPLOSION_THRESHOLD_MAX;

  for (auto &card : animated_cards_) {
    if (!card.active)
      continue;

    if (!card.exploded) {
      // Update position
      card.x += card.velocity_x;
      card.y += card.velocity_y;
      card.velocity_y += GRAVITY;

      // Update rotation
      card.rotation += card.rotation_velocity;

      // Check if card should explode (increase random chance from 2% to 5%)
      if (card.y > explosion_min && card.y < explosion_max &&
          (rand() % 100 < 5)) {
        explodeCard(card);
      }

      // Check if card is off screen
      if (card.x < -current_card_width_ || card.x > allocation.width ||
          card.y > allocation.height + current_card_height_) {
        card.active = false;
      } else {
        all_cards_finished = false;
      }
    } else {
      // Update explosion fragments
      updateCardFragments(card);

      // Check if all fragments are inactive
      bool all_fragments_inactive = true;
      for (const auto &fragment : card.fragments) {
        if (fragment.active) {
          all_fragments_inactive = false;
          all_cards_finished = false;
          break;
        }
      }

      if (all_fragments_inactive) {
        card.active = false;
      }
    }
  }

  // Clear inactive cards periodically to prevent memory bloat
  if (animated_cards_.size() > 200) {
    // Manual removal of inactive cards without using std::remove_if
    std::vector<AnimatedCard> active_cards;
    for (const auto& card : animated_cards_) {
      if (card.active) {
        active_cards.push_back(card);
      }
    }
    animated_cards_ = active_cards;
  }

  refreshDisplay();
}


void FreecellGame::launchNextCard() {
  // Try each foundation pile in sequence, cycling through them
  static int current_pile_index = 0;
  
  // Try all piles if needed
  for (int attempts = 0; attempts < foundation_.size(); attempts++) {
    int pile_index = (current_pile_index + attempts) % foundation_.size();
    
    // Check if this pile has any cards
    if (!foundation_[pile_index].empty()) {
      // Calculate the starting X position based on the pile
      // FIXED: Corrected the X position calculation for foundation piles
      double start_x = allocation.width - (4 - pile_index) * (current_card_width_ + current_card_spacing_);
      double start_y = current_card_spacing_;

      // Randomize launch trajectory
      int trajectory_choice = rand() % 100;
      int direction = rand() % 2;
      double speed = (15 + (rand() % 5)) * (direction ? 1 : -1);

      double angle;
      if (trajectory_choice < 5) {
        // 5% chance to go straight up
        angle = G_PI / 2 + (rand() % 200 - 100) / 1000.0 * G_PI / 8;
      } else if (trajectory_choice < 15) {
        // 10% chance for high arc launch
        angle = (rand() % 2 == 0) ? 
          (G_PI * 0.6 + (rand() % 500) / 1000.0 * G_PI / 6) : 
          (G_PI * 0.4 - (rand() % 500) / 1000.0 * G_PI / 6);
      } else {
        // Otherwise, spread left and right
        angle = trajectory_choice < 85 ? 
          (G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4) : 
          (G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4);
      }

      // Create animated card
      AnimatedCard anim_card;
      anim_card.card = foundation_[pile_index].back();
      anim_card.x = start_x;
      anim_card.y = start_y;
      anim_card.velocity_x = cos(angle) * speed;
      anim_card.velocity_y = sin(angle) * speed;
      anim_card.rotation = 0;
      anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;
      anim_card.active = true;
      anim_card.exploded = false;
      anim_card.face_up = true;
      anim_card.source_pile = pile_index;

      // Remove the card from the foundation pile
      cardlib::Card card = foundation_[pile_index].back();
      foundation_[pile_index].pop_back();
      
      // Add the card to the BOTTOM of the same foundation pile
      foundation_[pile_index].insert(foundation_[pile_index].begin(), card);

      // Add to animated cards
      animated_cards_.push_back(anim_card);
      cards_launched_++;
      
      // Move to the next pile for the next card
      current_pile_index = (pile_index + 1) % foundation_.size();
      return;
    }
  }
  
  // If we get here, there were no cards in any foundation pile
  // This shouldn't happen in normal gameplay, but just in case:
  current_pile_index = (current_pile_index + 1) % foundation_.size();
}

void FreecellGame::explodeCard(AnimatedCard &card) {

  // Check if card surface exists
  cairo_surface_t *card_surface = getCardSurface(card.card);
  if (!card_surface) {
    return;
  }

  // Mark the card as exploded
  card.exploded = true;

  playSound(GameSoundEvent::Firework);

  // Create fragments
  card.fragments.clear();

  // Split the card into smaller fragments for more dramatic effect (4x4 grid)
  const int grid_size = 4;
  const int fragment_width = current_card_width_ / grid_size;
  const int fragment_height = current_card_height_ / grid_size;

  for (int row = 0; row < grid_size; row++) {
    for (int col = 0; col < grid_size; col++) {
      CardFragment fragment;

      // Initial position
      fragment.x = card.x + col * fragment_width;
      fragment.y = card.y + row * fragment_height;
      fragment.width = fragment_width;
      fragment.height = fragment_height;

      // Calculate distance from center of the card
      double center_x = card.x + current_card_width_ / 2;
      double center_y = card.y + current_card_height_ / 2;
      double fragment_center_x = fragment.x + fragment_width / 2;
      double fragment_center_y = fragment.y + fragment_height / 2;

      // Direction vector from center of card
      double dir_x = fragment_center_x - center_x;
      double dir_y = fragment_center_y - center_y;

      // Normalize direction vector
      double magnitude = sqrt(dir_x * dir_x + dir_y * dir_y);
      if (magnitude > 0.001) {
        dir_x /= magnitude;
        dir_y /= magnitude;
      } else {
        // If fragment is at center, give it a random direction
        double rand_angle = 2.0 * G_PI * (rand() % 1000) / 1000.0;
        dir_x = cos(rand_angle);
        dir_y = sin(rand_angle);
      }

      // Velocity components
      double speed = 18.0 + (rand() % 12);
      double upward_bias = -20.0 - (rand() % 15);

      fragment.velocity_x = dir_x * speed + (rand() % 15 - 7);
      fragment.velocity_y = dir_y * speed + upward_bias * 1.5;

      // Rotation
      fragment.rotation = card.rotation;
      fragment.rotation_velocity = (rand() % 60 - 30) / 5.0;

      // Create a new image surface
      fragment.surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, fragment_width, fragment_height);

      if (cairo_surface_status(fragment.surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(fragment.surface);
        fragment.surface = nullptr;
        continue;
      }

      // Create a context for drawing on the new surface
      cairo_t *cr = cairo_create(fragment.surface);
      if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        cairo_surface_destroy(fragment.surface);
        fragment.surface = nullptr;
        continue;
      }

      // Draw white background (for transparency)
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
      cairo_rectangle(cr, 0, 0, fragment_width, fragment_height);
      cairo_fill(cr);

      // Draw the fragment of the card
      cairo_set_source_surface(cr, card_surface, -col * fragment_width,
                               -row * fragment_height);

      if (cairo_status(cr) == CAIRO_STATUS_SUCCESS) {
        cairo_rectangle(cr, 0, 0, fragment_width, fragment_height);
        cairo_fill(cr);
      }

      // Clean up the drawing context
      cairo_destroy(cr);

      // Set the fragment as active
      fragment.active = true;
      card.fragments.push_back(fragment);
    }
  }

  // We're using a cached surface, not creating a new one, so DON'T destroy it
  // cairo_surface_destroy(card_surface);
}

void FreecellGame::updateCardFragments(AnimatedCard &card) {
  if (!card.exploded)
    return;

  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);

  // Simple approach: just update existing fragments without creating new ones
  for (auto &fragment : card.fragments) {
    if (!fragment.active)
      continue;

    // Update position
    fragment.x += fragment.velocity_x;
    fragment.y += fragment.velocity_y;
    fragment.velocity_y += GRAVITY;

    // Update rotation
    fragment.rotation += fragment.rotation_velocity;

    // Check if fragment is in the lower part of the screen for potential "bounce" effect
    const double min_height = allocation.height * 0.5;
    if (fragment.y > min_height && fragment.y < allocation.height - fragment.height &&
        fragment.velocity_y > 0 && // Only when moving downward
        (rand() % 1000 < 5)) { // 0.5% chance per frame
      
      // Instead of creating new fragments, just give this one an upward boost
      // and maybe change its direction slightly
      fragment.velocity_y = -fragment.velocity_y * 0.8; // Reverse with reduced energy
      
      // Add a slight horizontal randomization
      fragment.velocity_x += (rand() % 11 - 5); // -5 to +5 adjustment
      
      // Increase rotation for visual effect
      fragment.rotation_velocity *= 1.5;
      
      // Play a sound for the "bounce"
      playSound(GameSoundEvent::Firework);
    }
    
    // Check if fragment is off screen
    if (fragment.x < -fragment.width || fragment.x > allocation.width ||
        fragment.y > allocation.height + fragment.height) {
      // Free the surface if it exists
      if (fragment.surface) {
        cairo_surface_destroy(fragment.surface);
        fragment.surface = nullptr;
      }
      fragment.active = false;
    }
  }
}

void FreecellGame::drawCardFragment(cairo_t *cr, const CardFragment &fragment) {
  // Skip inactive fragments or those without a surface
  if (!fragment.active) {
    return;
  }

  if (!fragment.surface) {
    return;
  }

  // Check surface status before using it
  if (cairo_surface_status(fragment.surface) != CAIRO_STATUS_SUCCESS) {
    return;
  }

  // Save the current transformation state
  cairo_save(cr);

  // Move to the center of the fragment for rotation
  cairo_translate(cr, fragment.x + fragment.width / 2,
                  fragment.y + fragment.height / 2);
  cairo_rotate(cr, fragment.rotation);

  // Draw the fragment
  cairo_set_source_surface(cr, fragment.surface, -fragment.width / 2,
                           -fragment.height / 2);

  // Only proceed if setting the source was successful
  if (cairo_status(cr) == CAIRO_STATUS_SUCCESS) {
    cairo_rectangle(cr, -fragment.width / 2, -fragment.height / 2,
                    fragment.width, fragment.height);
    cairo_fill(cr);
  }

  // Restore the transformation state
  cairo_restore(cr);
}

void FreecellGame::stopWinAnimation() {
  if (!win_animation_active_)
    return;

  win_animation_active_ = false;

  // Stop the animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  animated_cards_.clear();
  freecell_animation_cards_.clear();
  cards_launched_ = 0;
  launch_timer_ = 0;

  // Start new game
  initializeGame();
  refreshDisplay();
}


// This function starts the animation to move a card to the foundation
void FreecellGame::startFoundationMoveAnimation(const cardlib::Card &card, int source_pile, int source_index, int target_pile) {
  // If there's already an animation running, complete it immediately
  // before starting a new one to avoid race conditions
  if (foundation_move_animation_active_) {
    // Add the card to the foundation pile immediately
    foundation_[foundation_target_pile_].push_back(foundation_move_card_.card);

    // Check for win condition after adding the card
    if (checkWinCondition()) {
      // Stop the current animation and start win animation
      if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
      }
      foundation_move_animation_active_ = false;
      startWinAnimation();
      return;
    }
  }

  foundation_move_animation_active_ = true;
  foundation_source_pile_ = source_pile;
  foundation_target_pile_ = target_pile;
  foundation_move_timer_ = 0;

  // Calculate start position based on source pile
  double start_x, start_y;

  if (source_pile < 4) {
    // Freecell
    start_x = current_card_spacing_ + source_pile * (current_card_width_ + current_card_spacing_);
    start_y = current_card_spacing_;
  } else if (source_pile >= 8) {
    // Tableau pile
    int tableau_index = source_pile - 8;
    start_x = current_card_spacing_ + tableau_index * (current_card_width_ + current_card_spacing_);
    start_y = (2 * current_card_spacing_ + current_card_height_) + source_index * current_vert_spacing_;
  } else {
    // Shouldn't happen, but just in case
    foundation_move_animation_active_ = false;
    return;
  }

  // Calculate target position in foundation
  double target_x = allocation.width - (4 - target_pile) * (current_card_width_ + current_card_spacing_);
  double target_y = current_card_spacing_;
  
  // Initialize animation card
  foundation_move_card_.card = card;
  foundation_move_card_.x = start_x;
  foundation_move_card_.y = start_y;
  foundation_move_card_.target_x = target_x;
  foundation_move_card_.target_y = target_y;
  foundation_move_card_.velocity_x = 0;
  foundation_move_card_.velocity_y = 0;
  foundation_move_card_.rotation = 0;
  foundation_move_card_.rotation_velocity = 0;
  foundation_move_card_.active = true;
  foundation_move_card_.exploded = false;

  // Set up animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onFoundationMoveAnimationTick, this);

  // Force a redraw
  refreshDisplay();
}

// Timer callback for foundation move animation
gboolean FreecellGame::onFoundationMoveAnimationTick(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->updateFoundationMoveAnimation();
  return game->foundation_move_animation_active_ ? TRUE : FALSE;
}

// Update function for foundation move animation
void FreecellGame::updateFoundationMoveAnimation() {
  if (!foundation_move_animation_active_)
    return;

  // Calculate distance to target
  double dx = foundation_move_card_.target_x - foundation_move_card_.x;
  double dy = foundation_move_card_.target_y - foundation_move_card_.y;
  double distance = sqrt(dx * dx + dy * dy);

  if (distance < 5.0) {
    // Card has arrived at destination

    // Add card to the foundation pile
    foundation_[foundation_target_pile_].push_back(foundation_move_card_.card);

    // Play sound effect
    playSound(GameSoundEvent::CardPlace);

    // Mark animation as complete
    foundation_move_animation_active_ = false;

    // Stop the animation timer
    if (animation_timer_id_ > 0) {
      g_source_remove(animation_timer_id_);
      animation_timer_id_ = 0;
    }

    // Check if the player has won - BUT only if auto-finish is not active
    if (!auto_finish_active_ && checkWinCondition()) {
      startWinAnimation();
    }

    // Continue auto-finish if active
    if (auto_finish_active_) {
      // Set a short timer to avoid recursion
      if (auto_finish_timer_id_ > 0) {
        g_source_remove(auto_finish_timer_id_);
      }
      auto_finish_timer_id_ = g_timeout_add(50, onAutoFinishTick, this);
    }
  } else {
    // Move card toward destination with a smooth curve
    double speed = distance * FOUNDATION_MOVE_SPEED;
    double move_x = dx * speed / distance;
    double move_y = dy * speed / distance;

    // Add a slight arc to the motion
    double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
    double arc_height = 30.0; // Maximum height of the arc in pixels
    double arc_offset = sin(progress * G_PI) * arc_height;

    foundation_move_card_.x += move_x;
    foundation_move_card_.y += move_y - arc_offset * 0.1; // Apply a small amount of arc

    // Add a slight rotation
    foundation_move_card_.rotation = sin(progress * G_PI * 2) * 0.1;
  }

  refreshDisplay();
}

void FreecellGame::autoFinishGame() {
  // If auto-finish is already active, don't restart it
  if (auto_finish_active_ || foundation_move_animation_active_) {
    return;
  }

  // Explicitly deactivate keyboard navigation and selection
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;

  auto_finish_active_ = true;

  // Try to make the first move immediately
  processNextAutoFinishMove();
}

// Replace the existing processNextAutoFinishMove function with this improved version
void FreecellGame::processNextAutoFinishMove() {
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

  // Check freecells first
  for (int i = 0; i < freecells_.size(); i++) {
    if (!freecells_[i].has_value()) {
      continue;
    }

    const cardlib::Card &freecell_card = freecells_[i].value();

    // Try to move to foundation
    for (int f = 0; f < foundation_.size(); f++) {
      if (canMoveToFoundation(freecell_card, f)) {
        // Start the animation to move the card
        startFoundationMoveAnimation(freecell_card, i, 0, f);
        
        // Remove the card from the freecell
        freecells_[i] = std::nullopt;
        
        found_move = true;
        break;
      }
    }

    if (found_move) {
      break;
    }
  }

  // Try each tableau pile if no move was found yet
  if (!found_move) {
    for (int t = 0; t < tableau_.size(); t++) {
      auto &pile = tableau_[t];

      if (!pile.empty()) {
        const cardlib::Card &top_card = pile.back();

        // Try to move to foundation
        for (int f = 0; f < foundation_.size(); f++) {
          if (canMoveToFoundation(top_card, f)) {
            // Start the animation to move the card
            startFoundationMoveAnimation(top_card, t + 8, pile.size() - 1, f);
            
            // Remove the card from the tableau
            pile.pop_back();
            
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

// Auto-finish timer callback
gboolean FreecellGame::onAutoFinishTick(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  game->processNextAutoFinishMove();
  return FALSE; // Don't repeat the timer
}

cairo_surface_t* FreecellGame::getCardSurface(const cardlib::Card& card) {
  std::string key = std::to_string(static_cast<int>(card.suit)) +
                    std::to_string(static_cast<int>(card.rank));
  auto it = card_surface_cache_.find(key);
  
  if (it != card_surface_cache_.end()) {
    return it->second;
  }
  
  // Return nullptr if not found
  return nullptr;
}

void FreecellGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  resetKeyboardNavigation();

  playSound(GameSoundEvent::WinGame);
  // Show win message
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
      GTK_BUTTONS_OK, NULL);

  // Get the message area to apply formatting
  GtkWidget *message_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));

  // Create a label with centered text
  GtkWidget *label = gtk_label_new("Congratulations! You've won!\n\nClick or press any key to stop the celebration and start a new game");
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(label, 20);
  gtk_widget_set_margin_end(label, 20);
  gtk_widget_set_margin_top(label, 10);
  gtk_widget_set_margin_bottom(label, 10);

  // Add the label to the message area
  gtk_container_add(GTK_CONTAINER(message_area), label);
  gtk_widget_show(label);

  // Run the dialog
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  win_animation_active_ = true;
  cards_launched_ = 0;
  launch_timer_ = 0;
  animated_cards_.clear();

  // Initialize tracking for animated cards
  animated_foundation_cards_.clear();
  animated_foundation_cards_.resize(4); // 4 foundation piles
  for (size_t i = 0; i < 4; i++) {
    animated_foundation_cards_[i].resize(13, false); // 13 cards per pile
  }

  // Initialize freecell animation cards
  freecell_animation_cards_.clear();
  for (int i = 0; i < 4; i++) {
    std::vector<cardlib::Card> cards;
    // Copy all cards from all foundation piles
    for (const auto& pile : foundation_) {
      for (const auto& card : pile) {
        cards.push_back(card);
      }
    }
    // Use the deck's shuffle function instead of std::random_shuffle
    // Create a temporary deck with our cards
    cardlib::Deck temp_deck;
    temp_deck = cardlib::Deck(); // Clear the standard deck
    for (const auto& card : cards) {
      temp_deck.addCard(card);
    }
    temp_deck.shuffle(); // Shuffle using the deck's shuffle method
    
    // Extract the shuffled cards
    cards.clear();
    while (!temp_deck.isEmpty()) {
      auto card = temp_deck.drawCard();
      if (card.has_value()) {
        cards.push_back(card.value());
      }
    }
    
    freecell_animation_cards_.push_back(cards);
  }

  // Set up animation timer
  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
  animated_freecell_cards_.clear();
  animated_freecell_cards_.resize(4, false);
}

// Main drawing callback function
gboolean FreecellGame::onDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  // Get the widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  
  // Store allocation for use in highlighting
  game->allocation = allocation;

  // Initialize or resize the buffer surface if needed
  game->initializeDrawBuffer(allocation.width, allocation.height);
  
  // Clear buffer with green background
  cairo_set_source_rgb(game->buffer_cr_, 0.0, 0.5, 0.0);
  cairo_paint(game->buffer_cr_);

  // Draw all game elements to the buffer
  game->drawFreecells();
  game->drawFoundationPiles();
  game->drawTableau();
  game->drawDraggedCards();
  game->drawAnimations();
  
  // Draw keyboard navigation highlights if active
  if (game->keyboard_navigation_active_ || game->keyboard_selection_active_) {
    game->highlightSelectedCard(game->buffer_cr_);
  }

  // Copy buffer to window
  cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

// Initialize or resize the drawing buffer
void FreecellGame::initializeDrawBuffer(int width, int height) {
  // Create or resize buffer surface if needed
  if (!buffer_surface_ ||
      cairo_image_surface_get_width(buffer_surface_) != width ||
      cairo_image_surface_get_height(buffer_surface_) != height) {
    
    if (buffer_surface_) {
      cairo_surface_destroy(buffer_surface_);
      cairo_destroy(buffer_cr_);
    }

    buffer_surface_ = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    buffer_cr_ = cairo_create(buffer_surface_);
  }
}

// Draw the freecells (4 cells at the top-left)
void FreecellGame::drawFreecells() {
  int x = current_card_spacing_;
  int y = current_card_spacing_;
  
  // Number of freecells depends on game mode
  int num_freecells = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  
  for (int i = 0; i < num_freecells; i++) {
    if (i < freecells_.size()) {
      // Skip drawing the source card if it's being animated to foundation
      bool is_animated = foundation_move_animation_active_ && 
                         foundation_source_pile_ == i &&
                         freecells_[i].has_value() &&
                         foundation_move_card_.card.suit == freecells_[i].value().suit &&
                         foundation_move_card_.card.rank == freecells_[i].value().rank;
      
      // Skip drawing if this card is being dragged
      bool is_dragged = dragging_ && drag_source_pile_ == i &&
                        freecells_[i].has_value() && drag_card_.has_value() &&
                        drag_card_.value().suit == freecells_[i].value().suit &&
                        drag_card_.value().rank == freecells_[i].value().rank;
                       
      if (freecells_[i].has_value() && !is_animated && !is_dragged) {
        // Draw the card in this freecell
        drawCard(buffer_cr_, x, y, &(freecells_[i].value()));
      } else {
        // If win animation is active, draw a card from freecell_animation_cards if available
        if (win_animation_active_ && !freecell_animation_cards_.empty() && 
            i < freecell_animation_cards_.size() && !freecell_animation_cards_[i].empty()) {
          // Draw the top card of the corresponding animation pile
          drawCard(buffer_cr_, x, y, &(freecell_animation_cards_[i].back()));
        } else {
          // Draw empty freecell
          drawEmptyPile(buffer_cr_, x, y);
        }
      }
    }
    x += current_card_width_ + current_card_spacing_;
  }
}

// Draw the foundation piles (4 piles at the top-right)
void FreecellGame::drawFoundationPiles() {
  GtkAllocation alloc = allocation;
  
  // Number of foundation piles is always 4, but their position depends on the game mode
  // In Double FreeCell, we need to account for the extra freecells
  int foundation_start_x;
  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    foundation_start_x = alloc.width - 4 * (current_card_width_ + current_card_spacing_);
  } else {
    foundation_start_x = alloc.width - 4 * (current_card_width_ + current_card_spacing_);
  }
  
  int x = foundation_start_x;
  int y = current_card_spacing_;
  
  for (int i = 0; i < 4; i++) {
    if (i < foundation_.size()) {
      if (!foundation_[i].empty()) {
        // Skip drawing if this card is being dragged
        bool is_dragged = dragging_ && drag_source_pile_ == (i + 4) &&
                          drag_card_.has_value() &&
                          drag_card_.value().suit == foundation_[i].back().suit &&
                          drag_card_.value().rank == foundation_[i].back().rank;
        
        if (!is_dragged) {
          // Draw top card of the foundation pile
          drawCard(buffer_cr_, x, y, &(foundation_[i].back()));
        } else {
          // Draw empty foundation pile when dragging
          drawEmptyPile(buffer_cr_, x, y);
        }
      } else {
        // Draw empty foundation pile
        drawEmptyPile(buffer_cr_, x, y);
      }
    }
    x += current_card_width_ + current_card_spacing_;
  }
}

// Draw the tableau (8 columns below)
void FreecellGame::drawTableau() {
  int tableau_y = 2 * current_card_spacing_ + current_card_height_;
  
  // Number of tableau columns depends on game mode
  int num_tableau_columns = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 8 : 10;
  
  for (int i = 0; i < num_tableau_columns; i++) {
    int x = current_card_spacing_ + i * (current_card_width_ + current_card_spacing_);
    
    if (i < tableau_.size()) {
      // Draw cards in this column
      if (tableau_[i].empty()) {
        // Draw empty tableau spot
        drawEmptyPile(buffer_cr_, x, tableau_y);
      } else {
        if (deal_animation_active_) {
          drawTableauDuringDealAnimation(i, x, tableau_y);
        } else {
          // Normal drawing (not during animation)
          drawNormalTableauColumn(i, x, tableau_y);
        }
      }
    }
  }
}

// Draw a tableau column during deal animation
void FreecellGame::drawTableauDuringDealAnimation(int column_index, int x, int tableau_y) {
  // During animation, we need to know which cards have been dealt already
  int cards_in_this_column = (cards_dealt_ + 7 - column_index) / 8;
  
  for (int j = 0; j < cards_in_this_column && j < tableau_[column_index].size(); j++) {
    bool is_animating = false;
    for (const auto &anim_card : deal_cards_) {
      if (anim_card.active && anim_card.card.suit == tableau_[column_index][j].suit &&
          anim_card.card.rank == tableau_[column_index][j].rank) {
        is_animating = true;
        break;
      }
    }
    
    if (!is_animating) {
      int card_y = tableau_y + j * current_vert_spacing_;
      drawCard(buffer_cr_, x, card_y, &tableau_[column_index][j]);
    }
  }
}

// Draw a normal tableau column (not during animation)
void FreecellGame::drawNormalTableauColumn(int column_index, int x, int tableau_y) {
  for (size_t j = 0; j < tableau_[column_index].size(); j++) {
    // Skip dragged cards and cards being animated to foundation
    bool is_animated = foundation_move_animation_active_ && 
                       foundation_source_pile_ == column_index + 8 &&
                       j == tableau_[column_index].size() - 1 &&
                       foundation_move_card_.card.suit == tableau_[column_index][j].suit &&
                       foundation_move_card_.card.rank == tableau_[column_index][j].rank;
                       
    if (dragging_ && drag_source_pile_ >= 8 && 
        drag_source_pile_ - 8 == column_index && 
        j >= drag_source_card_idx_) {
      continue;  // Skip the card being dragged
    }
    
    if (!is_animated) {
      int card_y = tableau_y + j * current_vert_spacing_;
      drawCard(buffer_cr_, x, card_y, &tableau_[column_index][j]);
    }
  }
}

// Draw cards being dragged
void FreecellGame::drawDraggedCards() {
  if (dragging_ && drag_card_.has_value()) {
    int drag_x = static_cast<int>(drag_start_x_ - drag_offset_x_);
    int drag_y = static_cast<int>(drag_start_y_ - drag_offset_y_);
    
    // If dragging multiple cards from tableau, draw them all with proper spacing
    if (drag_source_pile_ >= 8 && drag_cards_.size() > 1) {
      for (size_t i = 0; i < drag_cards_.size(); i++) {
        int card_y = drag_y + i * current_vert_spacing_;
        drawCard(buffer_cr_, drag_x, card_y, &drag_cards_[i]);
      }
    } else {
      // Just draw the single card
      drawCard(buffer_cr_, drag_x, drag_y, &drag_card_.value());
    }
  }
}

// Draw all active animations
void FreecellGame::drawAnimations() {
  // Draw animated cards for deal animation
  if (deal_animation_active_) {
    for (const auto &anim_card : deal_cards_) {
      if (anim_card.active) {
        drawAnimatedCard(buffer_cr_, anim_card);
      }
    }
  }
  
  // Draw foundation move animation if active
  if (foundation_move_animation_active_) {
    drawAnimatedCard(buffer_cr_, foundation_move_card_);
  }
  
  // Draw win animation if active
  if (win_animation_active_) {
    drawWinAnimation();
  }
}

// Draw the win animation (exploding cards)
void FreecellGame::drawWinAnimation() {
  for (const auto &anim_card : animated_cards_) {
    if (!anim_card.active) {
      continue;
    }

    if (!anim_card.exploded) {
      // Draw the whole card with rotation
      drawAnimatedCard(buffer_cr_, anim_card);
    } else {
      // Draw all the fragments for this card
      for (const auto &fragment : anim_card.fragments) {
        if (fragment.active) {
          drawCardFragment(buffer_cr_, fragment);
        }
      }
    }
  }
}

void FreecellGame::launchCardFromFreecell() {
  // Use static variables to track state between calls
  static int current_pile_index = 0;
  
  // We'll use a copy of the foundation cards for our second animation source
  // Make sure we have freecell cards initialized for animation
  if (freecell_animation_cards_.empty()) {
    // Create a copy of all foundation cards for each freecell position
    for (int i = 0; i < 4; i++) {
      std::vector<cardlib::Card> cards;
      // Copy all cards from all foundation piles
      for (const auto& pile : foundation_) {
        for (const auto& card : pile) {
          cards.push_back(card);
        }
      }
      
      // Use the deck's shuffle function instead of std::random_shuffle
      // Create a temporary deck with our cards
      cardlib::Deck temp_deck;
      temp_deck = cardlib::Deck(); // Clear the standard deck
      for (const auto& card : cards) {
        temp_deck.addCard(card);
      }
      temp_deck.shuffle(); // Shuffle using the deck's shuffle method
      
      // Extract the shuffled cards
      cards.clear();
      while (!temp_deck.isEmpty()) {
        auto card = temp_deck.drawCard();
        if (card.has_value()) {
          cards.push_back(card.value());
        }
      }
      
      freecell_animation_cards_.push_back(cards);
    }
  }

  // Try each freecell position
  for (int attempts = 0; attempts < 4; attempts++) {
    int pile_index = (current_pile_index + attempts) % 4;
    
    // Check if this pile has any cards
    if (!freecell_animation_cards_[pile_index].empty()) {
      // Calculate the starting X position based on the freecell position
      double start_x = current_card_spacing_ + pile_index * (current_card_width_ + current_card_spacing_);
      double start_y = current_card_spacing_;

      // Randomize launch trajectory - OPPOSITE BIAS compared to foundation launches
      int trajectory_choice = rand() % 100;
      int direction = rand() % 2;
      double speed = (15 + (rand() % 5)) * (direction ? 1 : -1);

      double angle;
      if (trajectory_choice < 5) {
        // 5% chance to go straight up
        angle = G_PI / 2 + (rand() % 200 - 100) / 1000.0 * G_PI / 8;
      } else if (trajectory_choice < 15) {
        // 10% chance for high arc launch
        angle = (rand() % 2 == 0) ? 
          (G_PI * 0.6 + (rand() % 500) / 1000.0 * G_PI / 6) : 
          (G_PI * 0.4 - (rand() % 500) / 1000.0 * G_PI / 6);
      } else {
        // Otherwise, OPPOSITE bias - mainly launch right instead of left
        angle = trajectory_choice < 85 ? 
          (G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4) : 
          (G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4);
      }

      // Create animated card
      AnimatedCard anim_card;
      anim_card.card = freecell_animation_cards_[pile_index].back();
      anim_card.x = start_x;
      anim_card.y = start_y;
      anim_card.velocity_x = cos(angle) * speed;
      anim_card.velocity_y = sin(angle) * speed;
      anim_card.rotation = 0;
      anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;
      anim_card.active = true;
      anim_card.exploded = false;
      anim_card.face_up = true;
      anim_card.source_pile = pile_index + 100; // Mark as from freecell animation

      // Remove the card from the freecell animation pile
      cardlib::Card card = freecell_animation_cards_[pile_index].back();
      freecell_animation_cards_[pile_index].pop_back();
      
      // Add the card to the BOTTOM of the same freecell animation pile
      freecell_animation_cards_[pile_index].insert(freecell_animation_cards_[pile_index].begin(), card);

      // Add to animated cards
      animated_cards_.push_back(anim_card);
      cards_launched_++;
      
      // Move to the next pile for the next card
      current_pile_index = (pile_index + 1) % 4;
      return;
    }
  }
  
  // If we get here, there were no cards in any pile
  // This shouldn't happen if we've initialized correctly
  current_pile_index = (current_pile_index + 1) % 4;
}
