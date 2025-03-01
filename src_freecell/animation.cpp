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
