#include "solitaire.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

void SolitaireGame::updateWinAnimation() {
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

void SolitaireGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  resetKeyboardNavigation();

  playSound(GameSoundEvent::WinGame);
  // Show win message
GtkWidget *dialog = gtk_message_dialog_new(
    GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
    GTK_BUTTONS_OK, NULL);  // Set message text to NULL initially

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

  // Set up animation timer
  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
}

void SolitaireGame::stopWinAnimation() {
  if (!win_animation_active_)
    return;

  win_animation_active_ = false;

  // First, stop the animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  // Clean up fragment surfaces to prevent memory leaks
  for (auto &card : animated_cards_) {
    for (auto &fragment : card.fragments) {
      if (fragment.surface) {
        // Only destroy if the surface is valid
        if (cairo_surface_status(fragment.surface) == CAIRO_STATUS_SUCCESS) {
          cairo_surface_destroy(fragment.surface);
        }
        fragment.surface = nullptr;
      }
    }
    card.fragments.clear();
  }

  animated_cards_.clear();
  animated_foundation_cards_.clear();
  cards_launched_ = 0;
  launch_timer_ = 0;

  // Start new game
  initializeGame();
  refreshDisplay();
}

gboolean SolitaireGame::onAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::launchNextCard() {
  // Early exit if all cards have been launched
  if (cards_launched_ >= 52)
    return;

  // Create a vector to store valid foundation piles that still have cards
  std::vector<int> valid_piles;
  
  // Check each foundation pile
  for (size_t pile_index = 0; pile_index < foundation_.size(); pile_index++) {
    // Skip empty piles
    if (foundation_[pile_index].empty())
      continue;
      
    // Find the highest card in this pile that hasn't been animated yet
    for (int card_index = static_cast<int>(foundation_[pile_index].size()) - 1; card_index >= 0; card_index--) {
      // Check if this card has already been animated
      if (card_index < static_cast<int>(animated_foundation_cards_[pile_index].size()) && 
          !animated_foundation_cards_[pile_index][card_index]) {
        // This card is available for animation
        valid_piles.push_back(pile_index);
        break;
      }
    }
  }
  
  // If no valid piles found, return
  if (valid_piles.empty())
    return;
    
  // Select a random pile from the valid piles
  int random_pile_index = valid_piles[rand() % valid_piles.size()];
  
  // Find the highest card in this pile that hasn't been animated yet
  int card_index = -1;
  for (int i = static_cast<int>(foundation_[random_pile_index].size()) - 1; i >= 0; i--) {
    if (i < static_cast<int>(animated_foundation_cards_[random_pile_index].size()) && 
        !animated_foundation_cards_[random_pile_index][i]) {
      card_index = i;
      break;
    }
  }
  
  // If no valid card found, return
  if (card_index == -1)
    return;

  // Mark this specific card as animated in the tracking structure
  animated_foundation_cards_[random_pile_index][card_index] = true;

  // Calculate the starting X position based on the pile
  double start_x =
      current_card_spacing_ +
      (3 + random_pile_index) * (current_card_width_ + current_card_spacing_);
  double start_y = current_card_spacing_;

  // Randomly choose a launch trajectory (left or right)
  double angle;
/*  if (rand() % 2 == 0) {
    // Left trajectory
    angle = G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4;
  } else {
    // Right trajectory
    angle = G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4;
  }*/
  
// Randomly choose a launch trajectory (left, right, straight up, or high arc)
  int trajectory_choice = rand() % 100;  // Random number between 0-99
  
  // Randomize launch speed slightly
  int direction=rand() %2;
  
  double speed = (15 + (rand() % 5));
  if (direction==1) {
      speed*=-1;
  }

  if (trajectory_choice < 5) {
    // 5% chance to go straight up (with slight random variation)
    angle = G_PI / 2 + (rand() % 200 - 100) / 1000.0 * G_PI / 8;
  } else if (trajectory_choice < 15) {
    // 10% chance for high arc launch (steeper angle for higher trajectory)
    if (rand() % 2 == 0) {
      // High arc left
      angle = G_PI * 0.6 + (rand() % 500) / 1000.0 * G_PI / 6;
    } else {
      // High arc right
      angle = G_PI * 0.4 - (rand() % 500) / 1000.0 * G_PI / 6;
    }
    
  } else if (trajectory_choice < 55) {
    // 40% chance for left trajectory
    angle = G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4;
  } else {
    // 45% chance for right trajectory
    angle = G_PI * 1 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4;
  }
  

  // Create an animated card instance
  AnimatedCard anim_card;
  anim_card.card = foundation_[random_pile_index][card_index];
  anim_card.x = start_x;
  anim_card.y = start_y;

  // Calculate velocity components
  anim_card.velocity_x = cos(angle) * speed;
  anim_card.velocity_y = sin(angle) * speed;

  // Add some rotation for visual interest
  anim_card.rotation = 0;
  anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;

  // Set card as active and not yet exploded
  anim_card.active = true;
  anim_card.exploded = false;

  // Set card to face up
  anim_card.face_up = true;

  // Add to the list of animated cards
  animated_cards_.push_back(anim_card);
  cards_launched_++;
}

void SolitaireGame::updateCardFragments(AnimatedCard &card) {
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

void SolitaireGame::drawCardFragment(cairo_t *cr,
                                     const CardFragment &fragment) {
  // Skip inactive fragments or those without a surface
  if (!fragment.active || !fragment.surface)
    return;

  // Check surface status before using it
  if (cairo_surface_status(fragment.surface) != CAIRO_STATUS_SUCCESS)
    return;

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

gboolean SolitaireGame::onDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  // Get the widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);

  // Create or resize buffer surface if needed
  if (!game->buffer_surface_ ||
      cairo_image_surface_get_width(game->buffer_surface_) !=
          allocation.width ||
      cairo_image_surface_get_height(game->buffer_surface_) !=
          allocation.height) {

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

  // Draw stock pile
  int x = game->current_card_spacing_;
  int y = game->current_card_spacing_;
  if (!game->stock_.empty()) {
    game->drawCard(game->buffer_cr_, x, y, nullptr, false);
  } else {
    // Draw empty stock pile outline
    cairo_set_source_rgb(game->buffer_cr_, 0.2, 0.2, 0.2);
    cairo_rectangle(game->buffer_cr_, x, y, game->current_card_width_,
                    game->current_card_height_);
    cairo_stroke(game->buffer_cr_);
  }

  // Draw waste pile
  x += game->current_card_width_ + game->current_card_spacing_;
  if (!game->waste_.empty()) {
    // Check if the top card is being dragged
    bool top_card_dragging =
        (game->dragging_ && game->drag_source_pile_ == 1 &&
         game->drag_cards_.size() == 1 && game->waste_.size() >= 1 &&
         game->drag_cards_[0].suit == game->waste_.back().suit &&
         game->drag_cards_[0].rank == game->waste_.back().rank);

    if (top_card_dragging && game->waste_.size() > 1) {
      // Draw the second-to-top card
      const auto &second_card = game->waste_[game->waste_.size() - 2];
      game->drawCard(game->buffer_cr_, x, y, &second_card, true);
    } else if (!top_card_dragging) {
      // Draw the top card if it's not being dragged
      const auto &top_card = game->waste_.back();
      game->drawCard(game->buffer_cr_, x, y, &top_card, true);
    } else {
      // Draw an empty placeholder if top card is being dragged and there are no
      // other cards
      cairo_set_source_rgb(game->buffer_cr_, 0.2, 0.2, 0.2);
      cairo_rectangle(game->buffer_cr_, x, y, game->current_card_width_,
                      game->current_card_height_);
      cairo_stroke(game->buffer_cr_);
    }
  }

  // Draw foundation piles
  x = 3 * (game->current_card_width_ + game->current_card_spacing_);
  for (size_t i = 0; i < game->foundation_.size(); i++) {
    cairo_set_source_rgb(game->buffer_cr_, 0.2, 0.2, 0.2);
    cairo_rectangle(game->buffer_cr_, x, y, game->current_card_width_,
                    game->current_card_height_);
    cairo_stroke(game->buffer_cr_);

    const auto &pile = game->foundation_[i];
    if (!pile.empty()) {
      // Only draw the topmost non-animated card
      if (game->win_animation_active_) {
        for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
          if (!game->animated_foundation_cards_[i][j]) {
            game->drawCard(game->buffer_cr_, x, y, &pile[j], true);
            break;
          }
        }
      } else {
        // Check if the top card is being dragged from foundation
        bool top_card_dragging =
            (game->dragging_ && game->drag_source_pile_ == i + 2 &&
             !pile.empty() && game->drag_cards_.size() == 1 &&
             game->drag_cards_[0].suit == pile.back().suit &&
             game->drag_cards_[0].rank == pile.back().rank);

        if (!top_card_dragging) {
          const auto &top_card = pile.back();
          game->drawCard(game->buffer_cr_, x, y, &top_card, true);
        } else if (pile.size() > 1) {
          // Draw the second-to-top card
          const auto &second_card = pile[pile.size() - 2];
          game->drawCard(game->buffer_cr_, x, y, &second_card, true);
        }
      }
    }
    x += game->current_card_width_ + game->current_card_spacing_;
  }

  // Draw tableau piles
  const int tableau_base_y = game->current_card_spacing_ +
                             game->current_card_height_ +
                             game->current_vert_spacing_;

  for (size_t i = 0; i < game->tableau_.size(); i++) {
    x = game->current_card_spacing_ +
        i * (game->current_card_width_ + game->current_card_spacing_);
    const auto &pile = game->tableau_[i];

    // Draw empty pile outline
    if (pile.empty()) {
      cairo_set_source_rgb(game->buffer_cr_, 0.2, 0.2, 0.2);
      cairo_rectangle(game->buffer_cr_, x, tableau_base_y,
                      game->current_card_width_, game->current_card_height_);
      cairo_stroke(game->buffer_cr_);
    }

    // During animation, we need to know which cards have been dealt already
    if (game->deal_animation_active_) {
      // Figure out how many cards should be visible in this pile
      int cards_in_this_pile = i + 1; // Each pile has (index + 1) cards
      int total_cards_before_this_pile = 0;

      for (int p = 0; p < i; p++) {
        total_cards_before_this_pile += (p + 1);
      }

      // Only draw cards that have already been dealt and are not currently
      // animating
      int cards_to_draw = std::min(
          static_cast<int>(pile.size()),
          std::max(0, game->cards_dealt_ - total_cards_before_this_pile));

      for (int j = 0; j < cards_to_draw; j++) {
        // Skip drawing the card if it's currently animating
        bool is_animating = false;
        for (const auto &anim_card : game->deal_cards_) {
          if (anim_card.active && anim_card.card.suit == pile[j].card.suit &&
              anim_card.card.rank == pile[j].card.rank) {
            is_animating = true;
            break;
          }
        }

        if (!is_animating) {
          int current_y = tableau_base_y + j * game->current_vert_spacing_;
          game->drawCard(game->buffer_cr_, x, current_y, &pile[j].card,
                         pile[j].face_up);
        }
      }
    } else {
      // Normal drawing (not during animation)
      for (size_t j = 0; j < pile.size(); j++) {
        if (game->dragging_ && game->drag_source_pile_ >= 6 &&
            game->drag_source_pile_ - 6 == static_cast<int>(i) &&
            j >= static_cast<size_t>(game->tableau_[i].size() -
                                     game->drag_cards_.size())) {
          continue;
        }

        int current_y = tableau_base_y + j * game->current_vert_spacing_;
        const auto &tableau_card = pile[j];
        game->drawCard(game->buffer_cr_, x, current_y, &tableau_card.card,
                       tableau_card.face_up);
      }
    }
  }

  // Draw dragged cards
  if (game->stock_to_waste_animation_active_) {
    game->drawAnimatedCard(game->buffer_cr_, game->stock_to_waste_card_);
  }
  if (game->dragging_ && !game->drag_cards_.empty()) {
    int drag_x = static_cast<int>(game->drag_start_x_ - game->drag_offset_x_);
    int drag_y = static_cast<int>(game->drag_start_y_ - game->drag_offset_y_);

    for (size_t i = 0; i < game->drag_cards_.size(); i++) {
      game->drawCard(game->buffer_cr_, drag_x,
                     drag_y + i * game->current_vert_spacing_,
                     &game->drag_cards_[i], true);
    }
  }

  // Draw animated cards for win animation
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

  // Draw animated card for foundation move animation
  if (game->foundation_move_animation_active_) {
    game->drawAnimatedCard(game->buffer_cr_, game->foundation_move_card_);
  }

  // Draw animated cards for deal animation
  if (game->deal_animation_active_) {
    // Debug indicator - small red square to indicate deal animation is active
    cairo_set_source_rgb(game->buffer_cr_, 1.0, 0.0, 0.0);
    cairo_rectangle(game->buffer_cr_, 10, 10, 10, 10);
    cairo_fill(game->buffer_cr_);

    for (const auto &anim_card : game->deal_cards_) {
      if (!anim_card.active)
        continue;

      // Draw the card with rotation
      game->drawAnimatedCard(game->buffer_cr_, anim_card);
    }
  }

  if (game->keyboard_navigation_active_ && !game->dragging_ &&
      !game->deal_animation_active_ && !game->win_animation_active_ &&
      !game->foundation_move_animation_active_ &&
      !game->stock_to_waste_animation_active_) {
    game->highlightSelectedCard(game->buffer_cr_);
  }

  // Copy buffer to window
  cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

void SolitaireGame::explodeCard(AnimatedCard &card) {
  // Mark the card as exploded
  card.exploded = true;

  playSound(GameSoundEvent::Firework);

  // Create a surface for the card
  cairo_surface_t *card_surface = getCardSurface(card.card);
  if (!card_surface)
    return;

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
      double speed = 12.0 + (rand() % 8);
      double upward_bias = -15.0 - (rand() % 10);

      fragment.velocity_x = dir_x * speed + (rand() % 10 - 5);
      fragment.velocity_y = dir_y * speed + upward_bias;

      // Rotation
      fragment.rotation = card.rotation;
      fragment.rotation_velocity = (rand() % 60 - 30) / 5.0;

      // Create a new image surface
      fragment.surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, fragment_width, fragment_height);

      if (cairo_surface_status(fragment.surface) != CAIRO_STATUS_SUCCESS) {
        // Clean up and skip this fragment if we couldn't create the surface
        cairo_surface_destroy(fragment.surface);
        fragment.surface = nullptr;
        continue;
      }

      // Create a context for drawing on the new surface
      cairo_t *cr = cairo_create(fragment.surface);
      if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        // Clean up and skip this fragment if we couldn't create the context
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
  // here This was causing a double-free issue
  // cairo_surface_destroy(card_surface);
}

void SolitaireGame::startDealAnimation() {
  if (deal_animation_active_)
    return;

#ifdef DEBUG
  std::cout << "Starting deal animation" << std::endl; // Debug output
#endif

  deal_animation_active_ = true;
  cards_dealt_ = 0;
  deal_timer_ = 0;
  deal_cards_.clear();

  // Make sure we're not using the same timer ID as the win animation
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  // Set up a new animation timer with a different callback
  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onDealAnimationTick, this);

  // Deal the first card immediately
  dealNextCard();

  // Force a redraw to ensure we don't see the cards already in place
  refreshDisplay();
}

gboolean SolitaireGame::onDealAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateDealAnimation();
  return game->deal_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::updateDealAnimation() {
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
#ifdef DEBUG
      std::cout << "Card arrived at destination" << std::endl; // Debug output
#endif
    } else {
      // Move card toward destination with a more pronounced arc
      double speed =
          distance * 0.15 * DEAL_SPEED; // Reduced from 0.2 to make it slower
      double move_x = dx * speed / distance;
      double move_y = dy * speed / distance;

      // Add a slight arc to the motion (card rises then falls)
      double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
      double arc_height = 50.0; // Maximum height of the arc in pixels
      double arc_offset = sin(progress * G_PI) * arc_height;

      card.x += move_x;
      card.y += move_y - arc_offset * 0.1; // Apply a small amount of arc

      // Update rotation (gradually reduce to zero)
      card.rotation *= 0.95; // Changed from 0.9 to make rotation last longer

      all_cards_arrived = false;
    }
  }

  // Check if we're done dealing and all cards have arrived
  if (all_cards_arrived &&
      cards_dealt_ >= 28) { // 28 = total cards in initial tableau
#ifdef DEBUG
    std::cout << "All cards dealt, completing animation"
              << std::endl; // Debug output
#endif
    completeDeal();
  }

  refreshDisplay();
}

void SolitaireGame::dealNextCard() {
  if (cards_dealt_ >= 28)
    return;

#ifdef DEBUG
  std::cout << "Dealing card #" << cards_dealt_ + 1
            << std::endl; // Debug output
#endif

  // Calculate which tableau pile and position this card belongs to
  int pile_index = 0;
  int card_index = 0;
  int cards_so_far = 0;

  // Determine the pile and card index for the current card
  for (int i = 0; i < 7; i++) {
    if (cards_so_far + (i + 1) > cards_dealt_) {
      pile_index = i;
      card_index = cards_dealt_ - cards_so_far;
      break;
    }
    cards_so_far += (i + 1);
  }

  // Start position (from stock pile)
  double start_x = current_card_spacing_;
  double start_y = current_card_spacing_;

  // Calculate target position
  double target_x = current_card_spacing_ +
                    pile_index * (current_card_width_ + current_card_spacing_);
  double target_y =
      (current_card_spacing_ + current_card_height_ + current_vert_spacing_) +
      card_index * current_vert_spacing_;

  // Create animation card
  AnimatedCard anim_card;
  anim_card.card = tableau_[pile_index][card_index].card;
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
  anim_card.face_up = tableau_[pile_index][card_index].face_up;

  // Give it a bigger initial rotation to make it more visible
  anim_card.rotation = (rand() % 1256) / 100.0 - 6.28;

  // Add to animation list
  deal_cards_.push_back(anim_card);
  cards_dealt_++;
}

void SolitaireGame::completeDeal() {
  deal_animation_active_ = false;

  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  deal_cards_.clear();
  cards_dealt_ = 0;

  refreshDisplay();
}

void SolitaireGame::startFoundationMoveAnimation(const cardlib::Card &card,
                                                 int source_pile,
                                                 int source_index,
                                                 int target_pile) {
  // If there's already an animation running, complete it immediately
  // before starting a new one to avoid race conditions
  if (foundation_move_animation_active_) {
    // Add the card to the foundation pile immediately
    foundation_[foundation_target_pile_ - 2].push_back(
        foundation_move_card_.card);

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

#ifdef DEBUG
  std::cout << "Starting foundation move animation" << std::endl;
#endif

  foundation_move_animation_active_ = true;
  foundation_target_pile_ = target_pile;
  foundation_move_timer_ = 0;

  // Calculate start position based on source pile
  double start_x, start_y;

  if (source_pile == 1) {
    // Waste pile
    start_x = 2 * current_card_spacing_ + current_card_width_;
    start_y = current_card_spacing_;
  } else if (source_pile >= 6 && source_pile <= 12) {
    // Tableau pile
    int tableau_index = source_pile - 6;
    start_x = current_card_spacing_ +
              tableau_index * (current_card_width_ + current_card_spacing_);
    start_y =
        (current_card_spacing_ + current_card_height_ + current_vert_spacing_) +
        source_index * current_vert_spacing_;
  } else {
    // Shouldn't happen, but just in case
    foundation_move_animation_active_ = false;
    return;
  }

  // Calculate target position in foundation
  double target_x =
      current_card_spacing_ +
      (3 + (target_pile - 2)) * (current_card_width_ + current_card_spacing_);
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
  foundation_move_card_.face_up = true;

  // Set up animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onFoundationMoveAnimationTick, this);

  // Force a redraw
  refreshDisplay();
}

gboolean SolitaireGame::onFoundationMoveAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateFoundationMoveAnimation();
  return game->foundation_move_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::updateFoundationMoveAnimation() {
  if (!foundation_move_animation_active_)
    return;

  // Calculate distance to target
  double dx = foundation_move_card_.target_x - foundation_move_card_.x;
  double dy = foundation_move_card_.target_y - foundation_move_card_.y;
  double distance = sqrt(dx * dx + dy * dy);

  if (distance < 5.0) {
    // Card has arrived at destination

    // Add card to the foundation pile
    foundation_[foundation_target_pile_ - 2].push_back(
        foundation_move_card_.card);

    // Mark animation as complete
    foundation_move_animation_active_ = false;

    // Stop the animation timer
    if (animation_timer_id_ > 0) {
      g_source_remove(animation_timer_id_);
      animation_timer_id_ = 0;
    }

    // Check if the player has won - BUT only if auto-finish is not active
    // When auto-finishing, we'll check for win condition at the end
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

    // Add a slight arc to the motion (card rises then falls)
    double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
    double arc_height = 30.0; // Maximum height of the arc in pixels
    double arc_offset = sin(progress * G_PI) * arc_height;

    foundation_move_card_.x += move_x;
    foundation_move_card_.y +=
        move_y - arc_offset * 0.1; // Apply a small amount of arc

    // Add a slight rotation
    foundation_move_card_.rotation = sin(progress * G_PI * 2) * 0.1;
  }

  refreshDisplay();
}

void SolitaireGame::drawAnimatedCard(cairo_t *cr,
                                     const AnimatedCard &anim_card) {
  if (!anim_card.active)
    return;

  // Draw the card with rotation
  cairo_save(cr);

  // Move to card center for rotation
  cairo_translate(cr, anim_card.x + current_card_width_ / 2,
                  anim_card.y + current_card_height_ / 2);
  cairo_rotate(cr, anim_card.rotation);
  cairo_translate(cr, -current_card_width_ / 2, -current_card_height_ / 2);

  // Draw the card with its actual face-up status
  drawCard(cr, 0, 0, &anim_card.card, anim_card.face_up);

  cairo_restore(cr);
}

void SolitaireGame::startStockToWasteAnimation() {
  if (stock_to_waste_animation_active_ || stock_.empty())
    return;
  playSound(GameSoundEvent::CardFlip);
#ifdef DEBUG
  std::cout << "Starting stock to waste animation" << std::endl;
#endif

  stock_to_waste_animation_active_ = true;
  stock_to_waste_timer_ = 0;
  pending_waste_cards_.clear();

  // Determine how many cards to deal based on mode
  int cards_to_deal =
      draw_three_mode_ ? std::min(3, static_cast<int>(stock_.size())) : 1;

  // Save the cards that will be moved to waste pile
  for (int i = 0; i < cards_to_deal; i++) {
    if (!stock_.empty()) {
      pending_waste_cards_.push_back(stock_.back());
      stock_.pop_back();
    }
  }

  if (pending_waste_cards_.empty()) {
    stock_to_waste_animation_active_ = false;
    return;
  }

  // Prepare the first card for animation
  stock_to_waste_card_.card = pending_waste_cards_.back();
  stock_to_waste_card_.face_up = true;

  // Calculate start position (stock pile)
  stock_to_waste_card_.x = current_card_spacing_;
  stock_to_waste_card_.y = current_card_spacing_;

  // Calculate target position (waste pile)
  stock_to_waste_card_.target_x =
      2 * current_card_spacing_ + current_card_width_;
  stock_to_waste_card_.target_y = current_card_spacing_;

  // Initial rotation for visual appeal
  stock_to_waste_card_.rotation = 0;
  stock_to_waste_card_.rotation_velocity = 0;
  stock_to_waste_card_.active = true;
  stock_to_waste_card_.exploded = false;

  // Set up animation timer
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onStockToWasteAnimationTick, this);

  // Force initial redraw
  refreshDisplay();
}

gboolean SolitaireGame::onStockToWasteAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateStockToWasteAnimation();
  return game->stock_to_waste_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::updateStockToWasteAnimation() {
  if (!stock_to_waste_animation_active_)
    return;

  // Calculate distance to target
  double dx = stock_to_waste_card_.target_x - stock_to_waste_card_.x;
  double dy = stock_to_waste_card_.target_y - stock_to_waste_card_.y;
  double distance = sqrt(dx * dx + dy * dy);

  // Determine if the card has arrived at destination
  if (distance < 5.0) {

    // Card has arrived
    waste_.push_back(stock_to_waste_card_.card);
    pending_waste_cards_.pop_back();
    playSound(GameSoundEvent::CardPlace);

    // Check if there are more cards to animate
    if (!pending_waste_cards_.empty()) {
      // Set up the next card animation
      stock_to_waste_card_.card = pending_waste_cards_.back();
      stock_to_waste_card_.x = current_card_spacing_;
      stock_to_waste_card_.y = current_card_spacing_;
      stock_to_waste_card_.rotation = 0;
    } else {
      // All cards have been animated
      completeStockToWasteAnimation();
      return;
    }
  } else {
    // Move card toward destination with a smooth curve
    double speed = 0.3;
    double move_x = dx * speed;
    double move_y = dy * speed;

    // Add a slight arc and rotation for visual appeal
    double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
    double arc_height = 20.0; // Maximum height of the arc
    double arc_offset = sin(progress * G_PI) * arc_height;

    stock_to_waste_card_.x += move_x;
    stock_to_waste_card_.y += move_y - arc_offset * 0.1;

    // Add a slight rotation during flight
    stock_to_waste_card_.rotation = sin(progress * G_PI * 2) * 0.15;
  }

  refreshDisplay();
}

void SolitaireGame::completeStockToWasteAnimation() {
  stock_to_waste_animation_active_ = false;

  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  pending_waste_cards_.clear();
  refreshDisplay();
}
