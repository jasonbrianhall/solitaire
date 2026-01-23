#include "solitaire.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

gboolean SolitaireGame::onAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
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
#ifndef _WIN32
        if (rendering_engine_ == RenderingEngine::OPENGL) {
            explodeCard_gl(card);
        } else {
            explodeCard(card);
        }
#else
            explodeCard(card);
#endif
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

// Simple fix for the win animation in multi-deck mode
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

  // If we're not in single-deck mode, adjust the foundation piles for the animation
  if (current_game_mode_ != GameMode::STANDARD_KLONDIKE) {
    // Save the original foundation piles for potential restoration later if needed
    std::vector<std::vector<cardlib::Card>> original_foundation = foundation_;
    
    // Clear the foundation piles for animation
    foundation_.clear();
    foundation_.resize(4); // Always 4 foundation piles (1 deck) for animation
    
    // Create a standard ordered deck (Ace to King for each suit)
    for (int suit = 0; suit < 4; suit++) {
      foundation_[suit].clear();
      
      // Add cards Ace to King
      for (int rank = static_cast<int>(cardlib::Rank::ACE); 
           rank <= static_cast<int>(cardlib::Rank::KING); 
           rank++) {
        cardlib::Card card(static_cast<cardlib::Suit>(suit), 
                           static_cast<cardlib::Rank>(rank));
        foundation_[suit].push_back(card);
      }
    }
  }

  // Set up the animation tracking structure
  animated_foundation_cards_.clear();
  animated_foundation_cards_.resize(4); // Always 4 foundation piles (1 deck)
  for (size_t i = 0; i < 4; i++) {
    animated_foundation_cards_[i].resize(13, false); // 13 cards per pile
  }

  // Set up animation timer
  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
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

// Draw the stock pile (face-down cards to draw from)
void SolitaireGame::drawStockPile() {
  int x = current_card_spacing_;
  int y = current_card_spacing_;
  
  if (rendering_engine_ == RenderingEngine::OPENGL) {
      if (!stock_.empty()) {
        drawCard_gl(stock_.back(), x, y, false);
      } else {
        // Draw empty stock pile outline
        drawEmptyPile_gl(x, y);
      }
   } else if( rendering_engine_ == RenderingEngine::CAIRO) {
      if (!stock_.empty()) {
        drawCard(buffer_cr_, x, y, nullptr, false);
      } else {
        // Draw empty stock pile outline
        drawEmptyPile(buffer_cr_, x, y);
      }
   }
}
