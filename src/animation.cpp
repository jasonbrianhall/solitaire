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
    launchNextCard();
  }

  // Update physics for all active cards
  bool all_cards_finished = true;
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);

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

  // Stop animation if all cards are done and we've launched them all
  if (all_cards_finished && cards_launched_ >= 52) {
    stopWinAnimation();
  }

  refreshDisplay();
}

void SolitaireGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  // Show win message
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
      GTK_BUTTONS_OK, "Congratulations! You've won!");
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
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }
  animated_cards_.clear();
  animated_foundation_cards_.clear();
  cards_launched_ = 0;

  // Start new game
  initializeGame();
  refreshDisplay();
}

void SolitaireGame::launchNextCard() {
  if (cards_launched_ >= 52)
    return;

  // Calculate which foundation pile and card to launch
  int pile_index = cards_launched_ / 13;
  int card_index =
      12 - (cards_launched_ % 13); // Start with King (12) down to Ace (0)

  if (pile_index < foundation_.size() && card_index >= 0 &&
      card_index < static_cast<int>(foundation_[pile_index].size())) {

    // Mark card as animated
    animated_foundation_cards_[pile_index][card_index] = true;

    // Calculate start position
    double start_x =
        current_card_spacing_ +
        (3 + pile_index) * (current_card_width_ + current_card_spacing_);
    double start_y = current_card_spacing_;

    // Random velocities for variety
    double angle = G_PI * 3 / 4 + (rand() % 1000) / 1000.0 * G_PI / 4;
    double speed = 15 + (rand() % 5);

    AnimatedCard anim_card;
    anim_card.card = foundation_[pile_index][card_index];
    anim_card.x = start_x;
    anim_card.y = start_y;
    anim_card.velocity_x = cos(angle) * speed;
    anim_card.velocity_y = sin(angle) * speed;
    anim_card.rotation = 0;
    anim_card.rotation_velocity = (rand() % 20 - 10) / 10.0;
    anim_card.active = true;

    animated_cards_.push_back(anim_card);
    cards_launched_++;
  }
}

gboolean SolitaireGame::onAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
}
