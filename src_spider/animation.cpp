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
            if (cards_launched_ >= 104) // 8 sequences x 13 cards = 104 for Spider
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

  // If all cards are finished and we've launched them all or reached a restart point,
  // reset to start launching from beginning
  if (all_cards_finished) {
    // Check if we've launched all 104 cards
    if (cards_launched_ >= 104) {
      // Reset tracking for animated cards to allow reusing the piles
      for (size_t i = 0; i < animated_foundation_cards_.size(); i++) {
        std::fill(animated_foundation_cards_[i].begin(), animated_foundation_cards_[i].end(), false);
      }
      // Reset cards_launched_ counter to allow showing which cards we've used
      cards_launched_ = 0;
    }
  }

  refreshDisplay();
}

void SolitaireGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  // Disable auto-finish mode if it's active
  if (auto_finish_active_) {
    auto_finish_active_ = false;
    if (auto_finish_timer_id_ > 0) {
      g_source_remove(auto_finish_timer_id_);
      auto_finish_timer_id_ = 0;
    }
  }

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
  animated_foundation_cards_.resize(8); // 8 foundation piles for Spider
  for (size_t i = 0; i < 8; i++) {
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
  if (cards_launched_ >= 104)  // 8 suits x 13 cards = 104 for Spider
    return;

  // Create a vector to store valid foundation piles that still have cards
  std::vector<int> valid_piles;
  
  // For Spider, we use an array to track which cards have been animated from which sequence
  // We need to show all 8 sequences with cards K through A
  
  // Calculate which sequence and which card in the sequence we should launch next
  int sequence_index = cards_launched_ % 8;  // Which of the 8 sequences
  int card_rank = 13 - (cards_launched_ / 8) % 13;  // Rank within sequence (13=K, 12=Q, etc.)
  
  if (card_rank <= 0) {
    // We've gone through all ranks, start over with Kings
    card_rank = 13;
  }
  
  // Convert rank to corresponding card rank enum value
  cardlib::Rank rank = static_cast<cardlib::Rank>(card_rank);
  
  // Choose the suit based on the sequence
  // For Spider single-suit, we'd use the same suit, but to make it visually interesting,
  // let's use different suits for different sequences
  cardlib::Suit suit;
  
  // Determine suit based on sequence number
  // For 4-suit spider, we might do HEARTS, DIAMONDS, CLUBS, SPADES, repeat
  switch (sequence_index % 4) {
    case 0:
      suit = cardlib::Suit::HEARTS;
      break;
    case 1:
      suit = cardlib::Suit::DIAMONDS;
      break;
    case 2:
      suit = cardlib::Suit::CLUBS;
      break;
    case 3:
      suit = cardlib::Suit::SPADES;
      break;
  }
  
  // Mark this card as animated in our tracking
  animated_foundation_cards_[sequence_index % animated_foundation_cards_.size()][13 - card_rank] = true;

  // Calculate the starting X position based on the pile
  // Use sequence_index to distribute cards from different foundation piles
  double start_x =
      current_card_spacing_ +
      (sequence_index % 8) * (current_card_width_ + current_card_spacing_);
  double start_y = current_card_spacing_;

  // Randomly choose a launch trajectory (left, right, straight up, or high arc)
  int trajectory_choice = rand() % 100;  // Random number between 0-99
  
  // Randomize launch speed slightly
  int direction = rand() % 2;
  
  double speed = (15 + (rand() % 5));
  if (direction == 1) {
      speed *= -1;
  }

  double angle;
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
  
  // Create a card for the animation
  cardlib::Card card(suit, rank);

  // Create an animated card instance
  AnimatedCard anim_card;
  anim_card.card = card;
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
  game->initBufferSurface(allocation);

  // Draw all game elements
  game->drawBackground(game->buffer_cr_);
  game->drawStockPile(game->buffer_cr_);
  game->drawFoundationPiles(game->buffer_cr_);
  game->drawTableauPiles(game->buffer_cr_);
  game->drawDraggedCards(game->buffer_cr_);
  game->drawAnimations(game->buffer_cr_);
  
  // Draw keyboard navigation highlight if active
  if (game->keyboard_navigation_active_ && !game->dragging_ &&
      !game->deal_animation_active_ && !game->win_animation_active_ &&
      !game->foundation_move_animation_active_ &&
      !game->stock_to_waste_animation_active_) {
    game->drawKeyboardNavigation(game->buffer_cr_);
  }

  // Copy buffer to window
  cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

// Initialize or resize the buffer surface
void SolitaireGame::initBufferSurface(GtkAllocation &allocation) {
  if (!buffer_surface_ ||
      cairo_image_surface_get_width(buffer_surface_) != allocation.width ||
      cairo_image_surface_get_height(buffer_surface_) != allocation.height) {

    if (buffer_surface_) {
      cairo_surface_destroy(buffer_surface_);
      cairo_destroy(buffer_cr_);
    }

    buffer_surface_ = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
    buffer_cr_ = cairo_create(buffer_surface_);
  }
}

// Draw the game background
void SolitaireGame::drawBackground(cairo_t *cr) {
  // Clear buffer with lighter green background
  cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
  cairo_paint(cr);
}

// Draw the stock pile
void SolitaireGame::drawStockPile(cairo_t *cr) {
  int x = current_card_spacing_;
  int y = current_card_spacing_;
  
  // Add debug output to verify stock pile state
#ifdef DEBUG
  std::cout << "Stock pile size: " << stock_.size() << std::endl;
#endif
  
  if (stock_.empty()) {
    // Make this condition more explicit and ensure it draws an empty pile
    drawEmptyPile(cr, x, y, true);
  } else {
    // Only draw a card back when there are actually cards left
    drawCard(cr, x, y, nullptr, false);
  }
}

// Draw the foundation piles
// Modify the drawFoundationPiles function to show currently animating cards
void SolitaireGame::drawFoundationPiles(cairo_t *cr) {
  // Calculate the starting X position for foundation piles
  // Start after the stock pile, move to the right
  int foundation_x = 2 * current_card_spacing_ + current_card_width_;
  int foundation_y = current_card_spacing_;

  // Track how many completed sequences we have
  int completed_sequences = foundation_[0].size();

  // Special handling for win animation: display all 8 piles with proper cards
  if (win_animation_active_) {
    // During win animation, display 8 piles with appropriate cards for visual feedback
    for (int pile = 0; pile < 8; pile++) {
      // Calculate card to show based on animation progress
      cardlib::Suit suit;
      
      // Determine suit based on pile number
      switch (pile % 4) {
        case 0:
          suit = cardlib::Suit::HEARTS;
          break;
        case 1:
          suit = cardlib::Suit::DIAMONDS;
          break;
        case 2:
          suit = cardlib::Suit::CLUBS;
          break;
        case 3:
          suit = cardlib::Suit::SPADES;
          break;
      }
      
      // Determine which card to show in this foundation pile
      // Find the highest card that hasn't been animated yet
      cardlib::Rank rank = cardlib::Rank::KING; // Default to King
      
      // Look through the animation tracking to find the next unannotated card
      for (int i = 0; i < 13; i++) {
        if (pile < static_cast<int>(animated_foundation_cards_.size()) && 
            i < static_cast<int>(animated_foundation_cards_[pile].size()) &&
            !animated_foundation_cards_[pile][i]) {
          // This card hasn't been animated yet
          rank = static_cast<cardlib::Rank>(13 - i); // Convert index to rank (13=King, 12=Queen, etc.)
          break;
        }
      }
      
      // Create a card with the appropriate suit and rank
      cardlib::Card card(suit, rank);
      
      // Draw the card
      drawCard(cr, foundation_x, foundation_y, &card, true);
      
      // Move to next position
      foundation_x += current_card_width_ + current_card_spacing_;
    }
  } else {
    // Normal gameplay display
    for (int i = 0; i < 8; i++) {
      // Special case: if we're doing a sequence animation, show the current card being animated
      if (sequence_animation_active_ && i == completed_sequences) {
        // Find the most recently activated card that has arrived at the foundation
        bool foundation_card_found = false;
        for (int j = sequence_cards_.size() - 1; j >= 0; j--) {
          // Check if this card is active or has been active and reached its destination
          if (!sequence_cards_[j].active && 
              sequence_cards_[j].x == sequence_cards_[j].target_x && 
              sequence_cards_[j].y == sequence_cards_[j].target_y) {
            // This card has completed its animation to the foundation
            const cardlib::Card &card = sequence_cards_[j].card;
            drawCard(cr, foundation_x, foundation_y, &card, true);
            foundation_card_found = true;
            break;
          }
        }
        
        // If no card has arrived yet, draw an empty pile
        if (!foundation_card_found) {
          drawEmptyPile(cr, foundation_x, foundation_y, false);
        }
      }
      // Check if we have a completed sequence for this pile
      else if (i < completed_sequences) {
        // For each completed sequence, draw its card
        const cardlib::Card &card = foundation_[0][i];
        drawCard(cr, foundation_x, foundation_y, &card, true);
      } else {
        // Empty foundation slot
        drawEmptyPile(cr, foundation_x, foundation_y, false);
      }
      
      // Move to next position
      foundation_x += current_card_width_ + current_card_spacing_;
    }
  }
}

// Draw the tableau piles
void SolitaireGame::drawTableauPiles(cairo_t *cr) {
  const int tableau_base_y = current_card_spacing_ +
                           current_card_height_ +
                           current_vert_spacing_;

  for (size_t i = 0; i < tableau_.size(); i++) {
    int x = current_card_spacing_ +
          i * (current_card_width_ + current_card_spacing_);
    const auto &pile = tableau_[i];

    // Draw empty pile outline
    if (pile.empty()) {
      drawEmptyPile(cr, x, tableau_base_y, false);
    }

    // Handle drawing the tableau cards
    if (deal_animation_active_) {
      drawTableauPileDuringAnimation(cr, i, x, tableau_base_y);
    } else {
      drawNormalTableauPile(cr, i, x, tableau_base_y);
    }
  }
}

// Helper method to draw tableau pile during animation
void SolitaireGame::drawTableauPileDuringAnimation(cairo_t *cr, size_t pile_index, int x, int tableau_base_y) {
  const auto &pile = tableau_[pile_index];
  
  // Use Spider-specific distribution logic:
  // First 6 piles get 6 cards, last 4 piles get 5 cards
  int cards_in_this_pile = (pile_index < 6) ? 6 : 5;
  
  // Calculate how many cards should be in piles before this one
  int total_cards_before_this_pile = 0;
  for (int p = 0; p < pile_index; p++) {
    total_cards_before_this_pile += (p < 6) ? 6 : 5;
  }
  
  // Only draw cards that have already been dealt and are not currently animating
  int cards_to_draw = std::min(
      static_cast<int>(pile.size()),
      std::max(0, cards_dealt_ - total_cards_before_this_pile));
  
  for (int j = 0; j < cards_to_draw; j++) {
    // Skip drawing the card if it's currently animating
    bool is_animating = false;
    
    // More direct approach: Store pile/position information directly in the AnimatedCard
    for (const auto &anim_card : deal_cards_) {
      if (anim_card.active) {
        // Each animated card has been tagged with its target pile_index and card_index
        // If this is the exact card we're trying to draw now, don't draw it
        if (anim_card.target_pile_index == static_cast<int>(pile_index) && 
            anim_card.target_card_index == j) {
          is_animating = true;
          break;
        }
      }
    }
    
    if (!is_animating) {
      int current_y = tableau_base_y + j * current_vert_spacing_;
      drawCard(cr, x, current_y, &pile[j].card, pile[j].face_up);
    }
  }
}

// Helper method to draw normal tableau pile
void SolitaireGame::drawNormalTableauPile(cairo_t *cr, size_t pile_index, int x, int tableau_base_y) {
    const auto &pile = tableau_[pile_index];
    
    for (size_t j = 0; j < pile.size(); j++) {
        // Skip drawing cards that are being dragged
        if (dragging_ && drag_source_pile_ >= 6 &&
            drag_source_pile_ - 6 == static_cast<int>(pile_index) &&
            j >= static_cast<size_t>(pile.size() - drag_cards_.size())) {
            continue;
        }
        
        // Skip drawing cards that are being animated in a sequence
        bool skip_for_animation = false;
        if (sequence_animation_active_ && sequence_tableau_index_ == static_cast<int>(pile_index)) {
            // Check if this card is currently being animated
            for (size_t anim_idx = 0; anim_idx < sequence_cards_.size(); anim_idx++) {
                if (sequence_cards_[anim_idx].active) {
                    // Check if this is one of the positions that should be skipped
                    for (size_t pos_idx = 0; pos_idx < anim_idx; pos_idx++) {
                        int position = sequence_card_positions_[pos_idx] - pos_idx;
                        if (j == static_cast<size_t>(position)) {
                            skip_for_animation = true;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        
        if (skip_for_animation) {
            continue;
        }
        
        int current_y = tableau_base_y + j * current_vert_spacing_;
        const auto &tableau_card = pile[j];
        drawCard(cr, x, current_y, &tableau_card.card, tableau_card.face_up);
    }
}

// Draw dragged cards
void SolitaireGame::drawDraggedCards(cairo_t *cr) {
  // Draw stock to waste animation
  if (stock_to_waste_animation_active_) {
    drawAnimatedCard(cr, stock_to_waste_card_);
  }
  
  // Draw cards being dragged by the player
  if (dragging_ && !drag_cards_.empty()) {
    int drag_x = static_cast<int>(drag_start_x_ - drag_offset_x_);
    int drag_y = static_cast<int>(drag_start_y_ - drag_offset_y_);

    for (size_t i = 0; i < drag_cards_.size(); i++) {
      drawCard(cr, drag_x,
               drag_y + i * current_vert_spacing_,
               &drag_cards_[i], true);
    }
  }
}

// Draw win animation
void SolitaireGame::drawWinAnimation(cairo_t *cr) {
  for (const auto &anim_card : animated_cards_) {
    if (!anim_card.active)
      continue;

    if (!anim_card.exploded) {
      // Draw the whole card with rotation
      drawAnimatedCard(cr, anim_card);
    } else {
      // Draw all the fragments for this card
      for (const auto &fragment : anim_card.fragments) {
        if (fragment.active) {
          drawCardFragment(cr, fragment);
        }
      }
    }
  }
}

// Draw deal animation
void SolitaireGame::drawDealAnimation(cairo_t *cr) {
  // Debug indicator - small red square to indicate deal animation is active
  cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
  cairo_rectangle(cr, 10, 10, 10, 10);
  cairo_fill(cr);

  for (const auto &anim_card : deal_cards_) {
    if (anim_card.active) {
      // Draw the card with rotation
      drawAnimatedCard(cr, anim_card);
    }
  }
}

// Draw keyboard navigation highlight
void SolitaireGame::drawKeyboardNavigation(cairo_t *cr) {
  highlightSelectedCard(cr);
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
  //refreshDisplay();
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
      card.active = false; // Set to false when the card arrives
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
      cards_dealt_ >= 54) { // Updated to 54 for Spider (6 piles * 6 cards + 4 piles * 5 cards)
#ifdef DEBUG
    std::cout << "All cards dealt, completing animation"
              << std::endl; // Debug output
#endif
    completeDeal();
  }

  refreshDisplay();
}

void SolitaireGame::dealNextCard() {
  if (cards_dealt_ >= 54) // 54 = total cards in initial Spider tableau (6 piles * 6 cards + 4 piles * 5 cards)
    return;

#ifdef DEBUG
  std::cout << "Dealing card #" << cards_dealt_ + 1
            << std::endl; // Debug output
#endif

  // Calculate which tableau pile and position this card belongs to
  int pile_index = 0;
  int card_index = 0;
  int cards_so_far = 0;

  // Spider has 10 tableau piles
  // First 6 piles get 6 cards, last 4 piles get 5 cards
  // Determine the pile and card index for the current card
  for (int i = 0; i < 10; i++) {
    int pile_size = (i < 6) ? 6 : 5; // First 6 piles have 6 cards, last 4 have 5
    
    if (cards_so_far + pile_size > cards_dealt_) {
      pile_index = i;
      card_index = cards_dealt_ - cards_so_far;
      break;
    }
    cards_so_far += pile_size;
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
  anim_card.active = true; // Keep as boolean but store specific location data
  anim_card.target_pile_index = pile_index; // Store the destination pile index
  anim_card.target_card_index = card_index; // Store the card position in the pile
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

void SolitaireGame::startSequenceAnimation(int tableau_index) {
    if (sequence_animation_active_)
        return;
    
    sequence_animation_active_ = true;
    sequence_animation_timer_ = 0;
    next_card_index_ = 0; // Reset counter when starting a new animation
    sequence_cards_.clear();
    sequence_card_positions_.clear();
    
    // Store which tableau pile we're animating from
    sequence_tableau_index_ = tableau_index;
    
    auto& pile = tableau_[tableau_index];
    
    // We need at least 13 cards for a complete sequence
    if (pile.size() < 13) {
        sequence_animation_active_ = false;
        sequence_tableau_index_ = -1;
        return;
    }
    
    // Start from the end of the pile (the top-most card visible to player, which is the Ace)
    int top_position = pile.size() - 1;
    
    // We need to find Ace at the top position
    if (pile[top_position].card.rank != cardlib::Rank::ACE) {
        sequence_animation_active_ = false;
        sequence_tableau_index_ = -1;
        return;
    }
    
    // Check if we have 13 cards in descending sequence of the same suit
    cardlib::Suit suit = pile[top_position].card.suit;
    
    // We'll collect cards from Ace (i=0), 2 (i=1), ... King (i=12)
    for (int i = 0; i < 13; i++) {
        int pos = top_position - i;
        if (pos < 0) {
            sequence_animation_active_ = false;
            sequence_tableau_index_ = -1;
            return; // Not enough cards
        }
        
        // Check card validity
        if (!pile[pos].face_up || 
            pile[pos].card.suit != suit || 
            static_cast<int>(pile[pos].card.rank) != (1 + i)) { // ACE=1, 2=2, ... KING=13
            sequence_animation_active_ = false;
            sequence_tableau_index_ = -1;
            return;
        }
        
        // Store the position in the tableau for removal later
        sequence_card_positions_.push_back(pos);
        
        // Add card to animation sequence
        AnimatedCard anim_card;
        anim_card.card = pile[pos].card;
        
        // Calculate start position in tableau
        int card_x = current_card_spacing_ + 
                    tableau_index * (current_card_width_ + current_card_spacing_);
        int card_y = (current_card_spacing_ + current_card_height_ + 
                    current_vert_spacing_) + pos * current_vert_spacing_;
        
        anim_card.x = card_x;
        anim_card.y = card_y;
        
        // Calculate target position in foundation area
        int foundation_x_start = 2 * current_card_spacing_ + current_card_width_;
        int foundation_pile = foundation_[0].size(); // Current number of completed sequences
        
        // All cards in the sequence target the same foundation pile slot
        anim_card.target_x = foundation_x_start + 
                           foundation_pile * (current_card_width_ + current_card_spacing_);
        anim_card.target_y = current_card_spacing_; // Top of the screen
        
        anim_card.active = false; // Start inactive
        anim_card.face_up = true;
        anim_card.rotation = 0;
        anim_card.rotation_velocity = (rand() % 40 - 20) / 100.0; // Slight random rotation
        anim_card.exploded = false;
        
        // Add to animation sequence (cards will be in order: Ace, 2, 3, ..., King)
        sequence_cards_.push_back(anim_card);
    }
    
    // Save the King card for reference
    sequence_king_card_ = pile[top_position - 12].card; // King is 12 positions behind Ace
    
    // Play sound
    playSound(GameSoundEvent::WinGame);
    
    // Make the first card (Ace) active to start the animation
    if (!sequence_cards_.empty()) {
        sequence_cards_[0].active = true;
        next_card_index_ = 1; // Next card to activate will be index 1 (2)
    }
    
    // Set up animation timer
    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }
    
    animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onSequenceAnimationTick, this);
    
    // Force a redraw
    refreshDisplay();
}

gboolean SolitaireGame::onSequenceAnimationTick(gpointer data) {
    SolitaireGame *game = static_cast<SolitaireGame *>(data);
    game->updateSequenceAnimation();
    return game->sequence_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::drawAnimations(cairo_t *cr) {
    // Draw win animation
    if (win_animation_active_) {
        drawWinAnimation(cr);
    }

    // Draw foundation move animation
    if (foundation_move_animation_active_) {
        drawAnimatedCard(cr, foundation_move_card_);
    }
    
    // Draw sequence completion animation
    if (sequence_animation_active_) {
        for (const auto& card : sequence_cards_) {
            if (card.active) {
                drawAnimatedCard(cr, card);
            }
        }
    }

    // Draw deal animation
    if (deal_animation_active_) {
        drawDealAnimation(cr);
    }
}

void SolitaireGame::updateSequenceAnimation() {
    if (!sequence_animation_active_)
        return;
    
    // Check if all cards have reached their destination
    bool all_cards_arrived = true;
    
    for (auto& card : sequence_cards_) {
        if (!card.active)
            continue;
            
        // Calculate distance to target
        double dx = card.target_x - card.x;
        double dy = card.target_y - card.y;
        double distance = sqrt(dx * dx + dy * dy);
        
        if (distance < 5.0) {
            // Card has arrived
            card.x = card.target_x;
            card.y = card.target_y;
            card.active = false;
            playSound(GameSoundEvent::CardPlace);
            
            // Immediately request a redraw to show the card on the foundation
            refreshDisplay();
        } else {
            // Use a slower speed for the sequence animation
            double speed = distance * 0.1; // Reduced to make animation slower
            double move_x = dx * speed / distance;
            double move_y = dy * speed / distance;
            
            // Add a slight arc to the motion
            double progress = 1.0 - (distance / sqrt(dx * dx + dy * dy));
            double arc_height = 40.0; // Maximum height of the arc in pixels
            double arc_offset = sin(progress * G_PI) * arc_height;
            
            card.x += move_x;
            card.y += move_y - arc_offset * 0.1; // Apply a small amount of arc
            
            // Update rotation for visual interest
            card.rotation += card.rotation_velocity;
            
            all_cards_arrived = false;
        }
    }
    
    // Increase the delay between launching cards to be more visible
    sequence_animation_timer_ += ANIMATION_INTERVAL;
    
    // Use 250ms delay between cards for clearer visualization
    if (sequence_animation_timer_ >= 250) {
        sequence_animation_timer_ = 0;
        
        if (next_card_index_ < sequence_cards_.size()) {
            // IMPORTANT FIX: Instead of removing cards as we go, just mark which ones 
            // have been removed and handle all removals at the end to avoid index problems
            
            // Activate the next card for animation
            sequence_cards_[next_card_index_].active = true;
            next_card_index_++;
        }
    }
    
    // Check if animation is complete
    if (all_cards_arrived && next_card_index_ >= sequence_cards_.size()) {
        // Now that all animations are complete, remove the cards from the tableau
        if (sequence_tableau_index_ >= 0 && 
            sequence_tableau_index_ < static_cast<int>(tableau_.size())) {
            
            auto& pile = tableau_[sequence_tableau_index_];
            
            // Remove all cards in the sequence (from highest position to lowest)
            // By sorting positions in descending order, we avoid index shifts
            std::vector<int> positions_to_remove = sequence_card_positions_;
            std::sort(positions_to_remove.begin(), positions_to_remove.end(), 
                     [](int a, int b) { return a > b; });
                     
            for (int pos : positions_to_remove) {
                if (pos >= 0 && pos < static_cast<int>(pile.size())) {
                    pile.erase(pile.begin() + pos);
                }
            }
            
            // Flip the new top card if needed
            if (!pile.empty() && !pile.back().face_up) {
                pile.back().face_up = true;
                playSound(GameSoundEvent::CardFlip);
            }
        }
        
        completeSequenceAnimation();
    }
    
    refreshDisplay();
}

void SolitaireGame::completeSequenceAnimation() {
    sequence_animation_active_ = false;
    next_card_index_ = 0; // Reset the counter
    
    if (animation_timer_id_ > 0) {
        g_source_remove(animation_timer_id_);
        animation_timer_id_ = 0;
    }
    
    // In Spider, the Ace is added to represent the completed sequence
    // Get the card from the sequence that will be displayed in the foundation
    if (!sequence_cards_.empty()) {
        // Get the last card in our sequence (which should be the Ace)
        cardlib::Card aceCard = sequence_cards_.back().card;
        foundation_[0].push_back(aceCard);
    } else {
        // Fallback - use the suit from the King but set rank to Ace
        cardlib::Card aceCard(sequence_king_card_.suit, cardlib::Rank::ACE);
        foundation_[0].push_back(aceCard);
    }
    
    // The cards have already been removed from the tableau pile during
    // the animation process in updateSequenceAnimation(), so we don't need
    // to remove them again here.
    
    // Flip the new top card in the source tableau if needed
    if (sequence_tableau_index_ >= 0 && 
        sequence_tableau_index_ < static_cast<int>(tableau_.size()) &&
        !tableau_[sequence_tableau_index_].empty() && 
        !tableau_[sequence_tableau_index_].back().face_up) {
        
        tableau_[sequence_tableau_index_].back().face_up = true;
        playSound(GameSoundEvent::CardFlip);
    }
    
    // Reset tracking variables
    sequence_tableau_index_ = -1;
    sequence_card_positions_.clear();
    sequence_cards_.clear();
    
    // Check for win condition (8 completed sequences for Spider)
    if (foundation_[0].size() >= 8) {
        startWinAnimation();
    }
    
    // Force a redraw
    refreshDisplay();
}
