#include "pyramid.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// ============================================================================
// PYRAMID SOLITAIRE - ANIMATION SYSTEM
// ============================================================================
//
// GAME RULES - PYRAMID SOLITAIRE:
// ================================
// OBJECTIVE: Remove all 28 cards from the pyramid
//
// PYRAMID LAYOUT:
//   - Cards dealt in pyramid shape (7 rows, 28 total cards)
//   - Row 1: 1 card
//   - Row 2: 2 cards
//   - Row 3: 3 cards
//   - Row 4: 4 cards
//   - Row 5: 5 cards
//   - Row 6: 6 cards
//   - Row 7: 7 cards
//
// CARD REMOVAL RULES:
//   - Two cards can be removed if their ranks sum to 13
//   - Kings (K=13) can be removed alone
//   - Rank values: A=1, 2=2, ..., 10=10, J=11, Q=12, K=13
//
// VALID PAIR REMOVALS:
//   - A (1) + Q (12) = 13
//   - 2 + J (11) = 13
//   - 3 + 10 = 13
//   - 4 + 9 = 13
//   - 5 + 8 = 13
//   - 6 + 7 = 13
//   - K = 13 (alone)
//
// CARD ACCESSIBILITY:
//   - A card can only be removed if it's not covered by cards below it
//   - In a pyramid, a card is exposed if both positions below it are empty
//   - Cards are removed by pairing or kings alone
//
// STOCK AND WASTE:
//   - Draw one card at a time from stock to waste
//   - Waste card can be paired with any exposed pyramid card
//   - Waste card can be paired with top card of waste pile (if applicable)
//   - When stock runs out, typically game ends (or reshuffle in some variants)
//
// GAME MODES:
//   - STANDARD_PYRAMID: Single deck, classic 28-card pyramid
//   - DOUBLE_PYRAMID: Two decks, larger pyramid (54 cards)
//   - TRIPLE_PYRAMID: Three decks, even larger pyramid (104 cards)
//
// WIN CONDITION:
//   - All 28 cards removed from the pyramid
//   - Pyramid becomes completely empty
// ============================================================================


gboolean PyramidGame::onAnimationTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
}

void PyramidGame::updateCardFragments(AnimatedCard &card) {
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

void PyramidGame::updateWinAnimation() {
  if (!win_animation_active_)
    return;

  // Launch new cards periodically - faster launch for more exciting finale!
  launch_timer_ += ANIMATION_INTERVAL;
  if (launch_timer_ >= 40) { // Launch a new card every 40ms (was 100ms) - 2.5x faster!
    launch_timer_ = 0;
    if (rand() % 100 < 15) {  // Increased chance for rapid multi-launches
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
void PyramidGame::startWinAnimation() {
  if (win_animation_active_)
    return;

  resetKeyboardNavigation();

  // IMPORTANT: Gather ALL 52 cards into foundation for the celebration!
  // This creates the illusion that all cards went to foundation
  
  // Move all stock cards to foundation
  while (!stock_.empty()) {
    foundation_[0].push_back(stock_.back());
    stock_.pop_back();
  }
  
  // Move all waste cards to foundation
  while (!waste_.empty()) {
    foundation_[0].push_back(waste_.back());
    waste_.pop_back();
  }
  
  // Move all removed pyramid cards from tableau to foundation
  for (auto &tableau_pile : tableau_) {
    for (auto &card : tableau_pile) {
      if (card.removed) {
        foundation_[0].push_back(card.card);
      }
    }
  }

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
  if (current_game_mode_ != GameMode::STANDARD_PYRAMID) {
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

  // Set up the animation tracking structure to match actual foundation state
  // (Must match the actual number of cards in each pile!)
  animated_foundation_cards_.clear();
  animated_foundation_cards_.resize(foundation_.size());
  for (size_t i = 0; i < foundation_.size(); i++) {
    animated_foundation_cards_[i].resize(foundation_[i].size(), false);
  }

  // Set up animation timer
  animation_timer_id_ =
      g_timeout_add(ANIMATION_INTERVAL, onAnimationTick, this);
}

void PyramidGame::launchNextCard() {
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

void PyramidGame::stopWinAnimation() {
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

void PyramidGame::completeDeal() {
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
void PyramidGame::drawStockPile() {
  int x = current_card_spacing_;
  int y = current_card_spacing_;
  
  if (stock_.empty()) {
    // Draw empty stock pile outline
    if (rendering_engine_ == RenderingEngine::CAIRO) {
      drawEmptyPile(buffer_cr_, x, y);
    }
#ifdef USEOPENGL
    else if (rendering_engine_ == RenderingEngine::OPENGL) {
      drawEmptyPile_gl(x, y);
    }
#endif
  } else {
    // Check if the top card is being dragged
    bool top_card_dragging =
        (dragging_ && drag_source_pile_ == 0 &&
         drag_cards_.size() == 1 && stock_.size() >= 1 &&
         drag_cards_[0].suit == stock_.back().suit &&
         drag_cards_[0].rank == stock_.back().rank);

    if (!top_card_dragging) {
      // Draw the top card if it's not being dragged
      const auto &card = stock_.back();
      if (rendering_engine_ == RenderingEngine::CAIRO) {
        drawCard(buffer_cr_, x, y, &card, true);
      }
#ifdef USEOPENGL
      else if (rendering_engine_ == RenderingEngine::OPENGL) {
        drawCard_gl(card, x, y, true);
      }
#endif
    } else {
      // Draw empty placeholder if card is being dragged
#ifdef USEOPENGL
      if (rendering_engine_ == RenderingEngine::CAIRO) {
        drawEmptyPile(buffer_cr_, x, y);
      } else if (rendering_engine_ == RenderingEngine::OPENGL) {
        drawEmptyPile_gl(x, y);
      }
#else
      drawEmptyPile(buffer_cr_, x, y);
#endif
    }
  }
}

// Draw the waste pile (cards drawn from stock)
void PyramidGame::drawWastePile() {
  int x = current_card_spacing_ + current_card_width_ + current_card_spacing_;
  int y = current_card_spacing_;
  
  if (waste_.empty()) {
    drawEmptyPile(buffer_cr_, x, y);
    return;
  }
  
  // Check if the top card is being dragged
  bool top_card_dragging =
      (dragging_ && drag_source_pile_ == 1 &&
       drag_cards_.size() == 1 && waste_.size() >= 1 &&
       drag_cards_[0].suit == waste_.back().suit &&
       drag_cards_[0].rank == waste_.back().rank);

  if (top_card_dragging && waste_.size() > 1) {
    // Draw the second-to-top card
    const auto &second_card = waste_[waste_.size() - 2];
#ifdef USEOPENGL
    if (rendering_engine_ == RenderingEngine::CAIRO) {
        drawCard(buffer_cr_, x, y, &second_card, true);
    } else if (rendering_engine_ == RenderingEngine::OPENGL) {
        drawCard_gl(second_card, x, y, true);
    }
#else
    drawCard(buffer_cr_, x, y, &second_card, true);
#endif
    
  } else if (!top_card_dragging) {
    // Draw the top card if it's not being dragged
    const auto &top_card = waste_.back();
#ifdef USEOPENGL
    if( rendering_engine_ == RenderingEngine::CAIRO) {
        drawCard(buffer_cr_, x, y, &top_card, true);
    } else if (rendering_engine_ == RenderingEngine::OPENGL) {
        drawCard_gl(top_card, x, y, true);
    }
#else
        drawCard(buffer_cr_, x, y, &top_card, true);
#endif
  } else {
    // Draw an empty placeholder if top card is being dragged and there are no other cards
#ifdef USEOPENGL
    if( rendering_engine_ == RenderingEngine::CAIRO) {
        drawEmptyPile(buffer_cr_, x, y);
    } else if( rendering_engine_ == RenderingEngine::CAIRO) {
        drawEmptyPile_gl(x, y);
    }
#else
        drawEmptyPile(buffer_cr_, x, y);
#endif
  }
}

// Draw the foundation piles (removed cards that sum to 13 or Kings alone)
// In Pyramid Solitaire, this shows the pile of removed card pairs
void PyramidGame::drawFoundationPiles() {
  // Position foundation pile next to the discard pile on the right
  // Layout: Stock | Waste | Foundation (Removed Cards) | Discard
  int x = current_card_spacing_ + (3 * current_card_width_) + (3 * current_card_spacing_);
  int y = current_card_spacing_;
  
  // Draw foundation pile for removed cards (sum to 13)
  if (foundation_[0].empty()) {
    // Draw empty pile outline
    if (rendering_engine_ == RenderingEngine::CAIRO) {
      drawEmptyPile(buffer_cr_, x, y);
    }
#ifdef USEOPENGL            
    else if (rendering_engine_ == RenderingEngine::OPENGL) {
      drawEmptyPile_gl(x, y);
    }
#endif
  } else {
    // Draw the top card of the removed pile
    const auto &top_card = foundation_[0].back();
    if (rendering_engine_ == RenderingEngine::CAIRO) {
      drawCard(buffer_cr_, x, y, &top_card, true);
    }
#ifdef USEOPENGL            
    else if (rendering_engine_ == RenderingEngine::OPENGL) {
      drawCard_gl(top_card, x, y, true);
    }
#endif
  }
}

// Draw removed cards count (complementary display to foundation pile)
void PyramidGame::drawDiscardPile() {
  // This space can be used for card count or other UI elements
  // The actual removed cards are displayed in drawFoundationPiles()
  // Optionally show a count here
  return;
}

// Draw the tableau piles (the main playing area)
void PyramidGame::drawTableauPiles() {
    const int base_y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
    int screen_width = 1024;
    
    // Calculate pile index offsets
    int max_foundation_index = 2 + static_cast<int>(foundation_.size()) - 1;
    int first_tableau_index = max_foundation_index + 1;
    
    // Pyramid Solitaire layout:
    // tableau_[0] = row 1 (1 card)
    // tableau_[1] = row 2 (2 cards staggered)
    // tableau_[2] = row 3 (3 cards staggered)
    // ...
    // tableau_[6] = row 7 (7 cards staggered)
    //
    // Each card in a row overlaps the previous one
    // Rows also overlap each other vertically
    
    const int HORIZ_SPACING = current_card_width_ + 15;    // Card width plus 15 pixel gap
    const int VERT_OVERLAP = current_card_height_ / 2;     // Half card height between rows
    
    for (int row = 0; row < static_cast<int>(tableau_.size()); row++) {
        const auto &pile = tableau_[row];
        int num_cards_in_row = row + 1;  // row 0 has 1 card, row 1 has 2, etc
        
        // Calculate the width needed for this row when all cards are visible
        // With full card width plus gap spacing
        int row_width = current_card_width_ + (num_cards_in_row - 1) * HORIZ_SPACING;
        
        // Center this row horizontally
        int row_start_x = (screen_width - row_width) / 2;
        
        // Y position for this row (rows have small vertical overlap)
        int row_y = base_y + row * VERT_OVERLAP;
        
        // Draw all cards in this pile (which forms one row of the pyramid)
        for (int card_idx = 0; card_idx < pile.size(); card_idx++) {
            const auto &tableau_card = pile[card_idx];
            
            // Skip cards that have been removed (matched and sent to foundation)
            if (tableau_card.removed) {
                continue;
            }
            
            // DEAL ANIMATION FIX: Skip cards currently being animated in deal
            if (deal_animation_active_) {
                bool is_animating = false;
                for (const auto &anim_card : deal_cards_) {
                    if (anim_card.active &&
                        anim_card.card.suit == tableau_card.card.suit &&
                        anim_card.card.rank == tableau_card.card.rank) {
                        is_animating = true;
                        break;
                    }
                }
                if (is_animating) {
                    continue;  // Skip this card - it's being animated
                }
            }
            
            // Skip the card if it's currently being dragged
            int current_pile_index = first_tableau_index + row;
            if (dragging_ && drag_source_pile_ == current_pile_index && 
                drag_source_card_idx_ == card_idx) {
                continue;
            }
            
            // X position - each card is spaced by full card width plus gap
            int card_x = row_start_x + (card_idx * HORIZ_SPACING);
            int card_y = row_y;
            
            if (rendering_engine_ == RenderingEngine::CAIRO) {
                drawCard(buffer_cr_, card_x, card_y, &tableau_card.card, tableau_card.face_up);
            }
#ifdef USEOPENGL            
            else if (rendering_engine_ == RenderingEngine::OPENGL) {
                drawCard_gl(tableau_card.card, card_x, card_y, tableau_card.face_up);
            }
#endif
        }
        
        // Draw empty pile outline if this row/pile is empty
        if (pile.empty()) {
            int card_x = row_start_x;
            int card_y = row_y;
            
            if (rendering_engine_ == RenderingEngine::CAIRO) {
                drawEmptyPile(buffer_cr_, card_x, card_y);
            }
#ifdef USEOPENGL            
            else if (rendering_engine_ == RenderingEngine::OPENGL) {
                drawEmptyPile_gl(card_x, card_y);
            }
#endif
        }
    }
}

// Draw tableau piles during the deal animation
void PyramidGame::drawTableauDuringDealAnimation(size_t pile_index, const std::vector<TableauCard> &pile, int x, int base_y) {
  // Figure out how many cards should be visible in this pile
  int cards_in_this_pile = pile_index + 1; // Each pile has (index + 1) cards
  int total_cards_before_this_pile = 0;

  for (int p = 0; p < pile_index; p++) {
    total_cards_before_this_pile += (p + 1);
  }

  // Only draw cards that have already been dealt and are not currently animating
  int cards_to_draw = std::min(
      static_cast<int>(pile.size()),
      std::max(0, cards_dealt_ - total_cards_before_this_pile));

  for (int j = 0; j < cards_to_draw; j++) {
    // Skip drawing the card if it's currently being animated to this exact position
    bool is_animating = false;
    for (const auto &anim_card : deal_cards_) {
      if (anim_card.active) {
        // Calculate the target pile and position from the animation's target coordinates
        int target_pile_index = std::round((anim_card.target_x - current_card_spacing_) / 
                                          (current_card_width_ + current_card_spacing_));
        int target_card_index = std::round((anim_card.target_y - base_y) / current_vert_spacing_);
        
        // If this animation is targeting the current pile and position, don't draw the card
        if (target_pile_index == static_cast<int>(pile_index) && target_card_index == j) {
          is_animating = true;
          break;
        }
      }
    }

    if (!is_animating) {
      int current_y = base_y + j * current_vert_spacing_;
      if (rendering_engine_ == RenderingEngine::CAIRO) {
          drawCard(buffer_cr_, x, current_y, &pile[j].card, pile[j].face_up);
      } 
#ifdef USEOPENGL      
      else if (rendering_engine_ == RenderingEngine::OPENGL) {
          drawCard_gl(pile[j].card, x, current_y, pile[j].face_up);
      }
#endif      
    }
  }
}
