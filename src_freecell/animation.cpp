#include "freecell.h"
#include <gtk/gtk.h>

void FreecellGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  // Reset any active selections
  resetKeyboardNavigation();

  // Play win sound
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
  
  // Initialize animation tracking
  animated_cards_.clear();
  
  // Set up animation timer
  animation_timer_id_ = g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
}

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

  // Just implement a simple version with cards flying up and falling down
  for (auto &card : animated_cards_) {
    if (!card.active)
      continue;

    // Update position
    card.x += card.velocity_x;
    card.y += card.velocity_y;
    card.velocity_y += GRAVITY;

    // Update rotation
    card.rotation += card.rotation_velocity;

    // Check if card is off screen
    if (card.x < -current_card_width_ || card.x > allocation.width ||
        card.y > allocation.height + current_card_height_) {
      card.active = false;
    } else {
      all_cards_finished = false;
    }
  }

  // If all cards are done and we've launched them all, eventually stop
  if (all_cards_finished) {
    cards_launched_ = 0; // Reset for next batch of cards
  }

  refreshDisplay();
}

void FreecellGame::launchNextCard() {
  // Early exit if all cards have been launched
  if (cards_launched_ >= 52)
    return;

  // Create a vector of candidate foundation piles with cards
  std::vector<cardlib::Card> candidate_cards;
  
  // Look for cards in foundation piles
  for (const auto &pile : foundation_) {
    for (const auto &card : pile) {
      candidate_cards.push_back(card);
    }
  }
  
  // If no cards are found in foundation, no animation
  if (candidate_cards.empty())
    return;
  
  // Pick a random card from candidates
  cardlib::Card card = candidate_cards[rand() % candidate_cards.size()];
  
  // Get the widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);
  
  // Calculate random start position along the bottom edge
  double start_x = (rand() % static_cast<int>(allocation.width - current_card_width_));
  double start_y = allocation.height;

  // Randomly choose a launch trajectory
  double angle = (M_PI/2) + ((rand() % 200 - 100) / 100.0) * (M_PI/6);
  double speed = 10.0 + (rand() % 5);
  
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

  // Set card as active
  anim_card.active = true;
  anim_card.exploded = false;

  // Add to the list of animated cards
  animated_cards_.push_back(anim_card);
  cards_launched_++;
  
  // Play launch sound
  playSound(GameSoundEvent::Firework);
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
    double arc_offset = sin(progress * M_PI) * arc_height;

    foundation_move_card_.x += move_x;
    foundation_move_card_.y += move_y - arc_offset * 0.1; // Apply a small amount of arc

    // Add a slight rotation
    foundation_move_card_.rotation = sin(progress * M_PI * 2) * 0.1;
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
