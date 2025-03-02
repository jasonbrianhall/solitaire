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
        // Launch 4 cards in rapid succession
        for (int i = 0; i < 4; i++) {
            launchNextCard();
            
            // Check if we've reached the limit - break if needed
            if (cards_launched_ >= 52) 
                break;
        }
    } else {    
       launchNextCard();
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
  if (cards_launched_<=0)
  {
      all_cards_finished=true;
  }
  // Stop animation if all cards are done and we've launched them all
  /*if (all_cards_finished && cards_launched_ >= 52) {
    stopWinAnimation();
  } */
  
  if (all_cards_finished) {
  // Reset tracking for animated cards to allow reusing the piles
  for (size_t i = 0; i < animated_foundation_cards_.size(); i++) {
    std::fill(animated_foundation_cards_[i].begin(), animated_foundation_cards_[i].end(), false);
  }
  // Reset cards_launched_ counter to allow showing which cards we've used
  cards_launched_ = 0;
  }

  refreshDisplay();
}

void FreecellGame::launchNextCard() {
  // Early exit if all cards have been launched
  if (cards_launched_ >= 52)
    return;

  // State to track the current launch progress
  static cardlib::Rank current_rank = cardlib::Rank::KING;
  static int current_suit_index = 0;

  // Array to map indices to suits in a consistent order
  const cardlib::Suit suit_order[] = {
    cardlib::Suit::HEARTS, 
    cardlib::Suit::DIAMONDS, 
    cardlib::Suit::CLUBS, 
    cardlib::Suit::SPADES
  };

  // Look for the card of the current rank and current suit
  bool card_found = false;
  for (int attempts = 0; attempts < foundation_.size(); attempts++) {
    int pile_index = (current_suit_index + attempts) % foundation_.size();
    
    // Check if this pile has the current card
    if (!foundation_[pile_index].empty() && 
        foundation_[pile_index].back().rank == current_rank &&
        foundation_[pile_index].back().suit == suit_order[current_suit_index]) {
      
      // Mark this card as animated
      size_t card_index = foundation_[pile_index].size() - 1;
      if (card_index < animated_foundation_cards_[pile_index].size() && 
          !animated_foundation_cards_[pile_index][card_index]) {
        
        animated_foundation_cards_[pile_index][card_index] = true;
        
        // Calculate the starting X position based on the pile
        double start_x = current_card_spacing_ +
            (3 + pile_index) * (current_card_width_ + current_card_spacing_);
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
          angle = trajectory_choice < 55 ? 
            (G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4) : 
            (G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4);
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

        // Remove the card from the foundation pile
        foundation_[pile_index].pop_back();

        // Add to animated cards
        animated_cards_.push_back(anim_card);
        cards_launched_++;
        card_found = true;
        break;
      }
    }
  }

  // Move to next card/suit if current card found or not found
  current_suit_index++;
  if (current_suit_index >= 4) {
    current_suit_index = 0;
    
    // Move to next rank down
    switch (current_rank) {
      case cardlib::Rank::KING:
        current_rank = cardlib::Rank::QUEEN;
        break;
      case cardlib::Rank::QUEEN:
        current_rank = cardlib::Rank::JACK;
        break;
      case cardlib::Rank::JACK:
        current_rank = cardlib::Rank::TEN;
        break;
      case cardlib::Rank::TEN:
        current_rank = cardlib::Rank::NINE;
        break;
      case cardlib::Rank::NINE:
        current_rank = cardlib::Rank::EIGHT;
        break;
      case cardlib::Rank::EIGHT:
        current_rank = cardlib::Rank::SEVEN;
        break;
      case cardlib::Rank::SEVEN:
        current_rank = cardlib::Rank::SIX;
        break;
      case cardlib::Rank::SIX:
        current_rank = cardlib::Rank::FIVE;
        break;
      case cardlib::Rank::FIVE:
        current_rank = cardlib::Rank::FOUR;
        break;
      case cardlib::Rank::FOUR:
        current_rank = cardlib::Rank::THREE;
        break;
      case cardlib::Rank::THREE:
        current_rank = cardlib::Rank::TWO;
        break;
      case cardlib::Rank::TWO:
        current_rank = cardlib::Rank::ACE;
        break;
      case cardlib::Rank::ACE:
        // All cards have been launched
        break;
    }
  }
}

void FreecellGame::explodeCard(AnimatedCard &card) {
  // Debug: Check if the method is even being called
  printf("explodeCard called for card with suit %d, rank %d\n", 
         static_cast<int>(card.card.suit), static_cast<int>(card.card.rank));

  // Check if card surface exists
  cairo_surface_t *card_surface = getCardSurface(card.card);
  if (!card_surface) {
    printf("ERROR: No card surface found!\n");
    return;
  }

  // Mark the card as exploded
  card.exploded = true;
  printf("Card marked as exploded\n");

  playSound(GameSoundEvent::Firework);

  // Create fragments
  card.fragments.clear();

  // Split the card into smaller fragments for more dramatic effect (4x4 grid)
  const int grid_size = 4;
  const int fragment_width = current_card_width_ / grid_size;
  const int fragment_height = current_card_height_ / grid_size;

  printf("Creating fragments: grid_size=%d, fragment_width=%d, fragment_height=%d\n", 
         grid_size, fragment_width, fragment_height);

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
        printf("ERROR: Failed to create fragment surface\n");
        cairo_surface_destroy(fragment.surface);
        fragment.surface = nullptr;
        continue;
      }

      // Create a context for drawing on the new surface
      cairo_t *cr = cairo_create(fragment.surface);
      if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        printf("ERROR: Failed to create cairo context\n");
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
      } else {
        printf("ERROR: Failed to draw fragment surface\n");
      }

      // Clean up the drawing context
      cairo_destroy(cr);

      // Set the fragment as active
      fragment.active = true;
      card.fragments.push_back(fragment);
    }
  }

  printf("Fragments created: %zu\n", card.fragments.size());

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
    printf("Skipping inactive fragment\n");
    return;
  }

  if (!fragment.surface) {
    printf("ERROR: Fragment surface is null\n");
    return;
  }

  // Check surface status before using it
  if (cairo_surface_status(fragment.surface) != CAIRO_STATUS_SUCCESS) {
    printf("ERROR: Fragment surface status is not successful\n");
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
  } else {
    printf("ERROR: Failed to set fragment source surface\n");
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

  win_animation_active_ = true;
  cards_launched_ = 0;
  launch_timer_ = 0;
  
  // Initialize tracking for animated foundation cards
  animated_foundation_cards_.clear();
  animated_foundation_cards_.resize(4);
  for (auto& pile : animated_foundation_cards_) {
    pile.resize(13, false);  // Each foundation can have at most 13 cards
  }
  
  // Clear any existing animated cards
  animated_cards_.clear();
  
  // Set up animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
  }
  
  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
  
  // Play win sound
  playSound(GameSoundEvent::WinGame);
  
  refreshDisplay();
}

gboolean FreecellGame::onDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  // Get the widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  
  // Store allocation for use in highlighting
  game->allocation = allocation;

  // Create or resize buffer surface if needed
  if (!game->buffer_surface_ ||
      cairo_image_surface_get_width(game->buffer_surface_) != allocation.width ||
      cairo_image_surface_get_height(game->buffer_surface_) != allocation.height) {
    
    if (game->buffer_surface_) {
      cairo_surface_destroy(game->buffer_surface_);
      cairo_destroy(game->buffer_cr_);
    }

    game->buffer_surface_ = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
    game->buffer_cr_ = cairo_create(game->buffer_surface_);
  }

  // Clear buffer with green background
  cairo_set_source_rgb(game->buffer_cr_, 0.0, 0.5, 0.0);
  cairo_paint(game->buffer_cr_);

  // Draw freecells (4 cells at the top-left)
  int x = game->current_card_spacing_;
  int y = game->current_card_spacing_;
  
  for (int i = 0; i < 4; i++) {
    if (i < game->freecells_.size()) {
      // Skip drawing the source card if it's being animated to foundation
      bool is_animated = game->foundation_move_animation_active_ && 
                         game->foundation_source_pile_ == i &&
                         game->freecells_[i].has_value() &&
                         game->foundation_move_card_.card.suit == game->freecells_[i].value().suit &&
                         game->foundation_move_card_.card.rank == game->freecells_[i].value().rank;
                         
      if (game->freecells_[i].has_value() && !is_animated) {
        // Draw the card in this freecell
        game->drawCard(game->buffer_cr_, x, y, &(game->freecells_[i].value()));
      } else {
        // Draw empty freecell
        game->drawEmptyPile(game->buffer_cr_, x, y);
      }
    }
    x += game->current_card_width_ + game->current_card_spacing_;
  }

  // Draw foundation piles (4 piles at the top-right)
  x = allocation.width - 4 * (game->current_card_width_ + game->current_card_spacing_);
  
  for (int i = 0; i < 4; i++) {
    if (i < game->foundation_.size()) {
      if (!game->foundation_[i].empty()) {
        // Draw top card of the foundation pile
        game->drawCard(game->buffer_cr_, x, y, &(game->foundation_[i].back()));
      } else {
        // Draw empty foundation pile
        game->drawEmptyPile(game->buffer_cr_, x, y);
      }
    }
    x += game->current_card_width_ + game->current_card_spacing_;
  }

  // Draw tableau (8 columns below)
  int tableau_y = 2 * game->current_card_spacing_ + game->current_card_height_;
  
  for (int i = 0; i < 8; i++) {
    x = game->current_card_spacing_ + i * (game->current_card_width_ + game->current_card_spacing_);
    
    if (i < game->tableau_.size()) {
      // Draw cards in this column
      if (game->tableau_[i].empty()) {
        // Draw empty tableau spot
        game->drawEmptyPile(game->buffer_cr_, x, tableau_y);
      } else {
        // During animation, we need to know which cards have been dealt already
        if (game->deal_animation_active_) {
          int cards_in_this_column = (game->cards_dealt_ + 7 - i) / 8;
          
          for (int j = 0; j < cards_in_this_column && j < game->tableau_[i].size(); j++) {
            bool is_animating = false;
            for (const auto &anim_card : game->deal_cards_) {
              if (anim_card.active && anim_card.card.suit == game->tableau_[i][j].suit &&
                  anim_card.card.rank == game->tableau_[i][j].rank) {
                is_animating = true;
                break;
              }
            }
            
            if (!is_animating) {
              int card_y = tableau_y + j * game->current_vert_spacing_;
              game->drawCard(game->buffer_cr_, x, card_y, &game->tableau_[i][j]);
            }
          }
        } else {
          // Normal drawing (not during animation)
          for (size_t j = 0; j < game->tableau_[i].size(); j++) {
            // Skip dragged cards and cards being animated to foundation
            bool is_animated = game->foundation_move_animation_active_ && 
                               game->foundation_source_pile_ == i + 8 &&
                               j == game->tableau_[i].size() - 1 &&
                               game->foundation_move_card_.card.suit == game->tableau_[i][j].suit &&
                               game->foundation_move_card_.card.rank == game->tableau_[i][j].rank;
                               
            if (game->dragging_ && game->drag_source_pile_ >= 8 && 
                game->drag_source_pile_ - 8 == i && 
                j >= game->drag_source_card_idx_) {
              continue;  // Skip the card being dragged
            }
            
            if (!is_animated) {
              int card_y = tableau_y + j * game->current_vert_spacing_;
              game->drawCard(game->buffer_cr_, x, card_y, &game->tableau_[i][j]);
            }
          }
        }
      }
    }
  }

  // Draw dragged card
  if (game->dragging_ && game->drag_card_.has_value()) {
    int drag_x = static_cast<int>(game->drag_start_x_ - game->drag_offset_x_);
    int drag_y = static_cast<int>(game->drag_start_y_ - game->drag_offset_y_);
    
    // If dragging multiple cards from tableau, draw them all with proper spacing
    if (game->drag_source_pile_ >= 8 && game->drag_cards_.size() > 1) {
      for (size_t i = 0; i < game->drag_cards_.size(); i++) {
        int card_y = drag_y + i * game->current_vert_spacing_;
        game->drawCard(game->buffer_cr_, drag_x, card_y, &game->drag_cards_[i]);
      }
    } else {
      // Just draw the single card
      game->drawCard(game->buffer_cr_, drag_x, drag_y, &game->drag_card_.value());
    }
  }

  // Draw animated cards for deal animation
  if (game->deal_animation_active_) {
    for (const auto &anim_card : game->deal_cards_) {
      if (!anim_card.active)
        continue;

      game->drawAnimatedCard(game->buffer_cr_, anim_card);
    }
  }
  
  // Draw foundation move animation if active
  if (game->foundation_move_animation_active_) {
    game->drawAnimatedCard(game->buffer_cr_, game->foundation_move_card_);
  }
  
  // Draw win animation if active
  if (game->win_animation_active_) {
    for (const auto &anim_card : game->animated_cards_) {
      if (!anim_card.active)
        continue;

      if (!anim_card.exploded) {
        // Draw the whole card with rotation
        game->drawAnimatedCard(game->buffer_cr_, anim_card);
      } else {
        // Draw all the fragments for this card
        for (const auto &fragment : anim_card.fragments) {
          if (fragment.active) {
            game->drawCardFragment(game->buffer_cr_, fragment);
          }
        }
      }
    }
  }  
  
  // Draw keyboard navigation highlights if active
  if (game->keyboard_navigation_active_ || game->keyboard_selection_active_) {
    game->highlightSelectedCard(game->buffer_cr_);
  }

  // Copy buffer to window
  cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

