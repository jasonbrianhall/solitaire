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

      // Check if card should explode (random chance within threshold area)
      if (card.y > explosion_min && card.y < explosion_max && (rand() % 100 < 2)) {
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
      for (const auto& fragment : card.fragments) {
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

void SolitaireGame::launchNextCard() {
  if (cards_launched_ >= 52)
    return;

  // Calculate which foundation pile and card to launch
  int pile_index = cards_launched_ / 13;
  int card_index = 12 - (cards_launched_ % 13); // Start with King (12) down to Ace (0)

  if (pile_index < foundation_.size() && card_index >= 0 &&
      card_index < static_cast<int>(foundation_[pile_index].size())) {

    // Mark card as animated
    animated_foundation_cards_[pile_index][card_index] = true;

    // Calculate start position
    double start_x = current_card_spacing_ + 
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
    anim_card.exploded = false; // Initialize the new exploded flag
    
    animated_cards_.push_back(anim_card);
    cards_launched_++;
  }
}

void SolitaireGame::stopWinAnimation() {
  if (!win_animation_active_)
    return;

  win_animation_active_ = false;
  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }
  
  // Clean up fragment surfaces to prevent memory leaks
  for (auto &card : animated_cards_) {
    for (auto &fragment : card.fragments) {
      if (fragment.surface) {
        cairo_surface_destroy(fragment.surface);
        fragment.surface = NULL;
      }
    }
  }
  
  animated_cards_.clear();
  animated_foundation_cards_.clear();
  cards_launched_ = 0;

  // Start new game
  initializeGame();
  refreshDisplay();
}


gboolean SolitaireGame::onAnimationTick(gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->updateWinAnimation();
  return game->win_animation_active_ ? TRUE : FALSE;
}

void SolitaireGame::explodeCard(AnimatedCard& card) {
  // Mark the card as exploded
  card.exploded = true;
  
  // Create a surface for the card
  cairo_surface_t* card_surface = getCardSurface(card.card);
  if (!card_surface) return;
  
  // Create fragments
  card.fragments.clear();
  
  // Split the card into fragments (2x2 grid)
  const int fragment_width = current_card_width_ / 2;
  const int fragment_height = current_card_height_ / 2;
  
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 2; col++) {
      CardFragment fragment;
      
      // Initial position (center of the original card)
      fragment.x = card.x + col * fragment_width;
      fragment.y = card.y + row * fragment_height;
      fragment.width = fragment_width;
      fragment.height = fragment_height;
      
      // Random velocities (exploding outward from center)
      double angle = 2.0 * G_PI * (rand() % 1000) / 1000.0;
      double speed = 5.0 + (rand() % 5);
      
      fragment.velocity_x = cos(angle) * speed;
      fragment.velocity_y = sin(angle) * speed;
      
      // Random rotation
      fragment.rotation = card.rotation;
      fragment.rotation_velocity = (rand() % 20 - 10) / 5.0; // Faster rotation than whole cards
      
      // Create a surface for this fragment
      fragment.surface = cairo_surface_create_similar(
          card_surface,
          cairo_surface_get_content(card_surface),
          fragment_width,
          fragment_height
      );
      
      // Copy the appropriate portion of the card to the fragment surface
      cairo_t* cr = cairo_create(fragment.surface);
      cairo_set_source_surface(cr, card_surface, -col * fragment_width, -row * fragment_height);
      cairo_rectangle(cr, 0, 0, fragment_width, fragment_height);
      cairo_fill(cr);
      cairo_destroy(cr);
      
      fragment.active = true;
      card.fragments.push_back(fragment);
    }
  }
}

void SolitaireGame::updateCardFragments(AnimatedCard& card) {
  if (!card.exploded)
    return;
    
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);
  
  for (auto& fragment : card.fragments) {
    if (!fragment.active)
      continue;
      
    // Update position
    fragment.x += fragment.velocity_x;
    fragment.y += fragment.velocity_y;
    fragment.velocity_y += GRAVITY;
    
    // Update rotation
    fragment.rotation += fragment.rotation_velocity;
    
    // Check if fragment is off screen
    if (fragment.x < -fragment.width || fragment.x > allocation.width ||
        fragment.y > allocation.height + fragment.height) {
      // Free the surface
      if (fragment.surface) {
        cairo_surface_destroy(fragment.surface);
        fragment.surface = NULL;
      }
      fragment.active = false;
    }
  }
}

void SolitaireGame::drawCardFragment(cairo_t* cr, const CardFragment& fragment) {
  if (!fragment.active || !fragment.surface)
    return;
    
  // Save the current transformation state
  cairo_save(cr);
  
  // Move to the center of the fragment for rotation
  cairo_translate(cr, fragment.x + fragment.width / 2, fragment.y + fragment.height / 2);
  cairo_rotate(cr, fragment.rotation);
  
  // Draw the fragment
  cairo_set_source_surface(cr, fragment.surface, -fragment.width / 2, -fragment.height / 2);
  cairo_rectangle(cr, -fragment.width / 2, -fragment.height / 2, fragment.width, fragment.height);
  cairo_fill(cr);
  
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
    const auto &top_card = game->waste_.back();
    game->drawCard(game->buffer_cr_, x, y, &top_card, true);
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
        const auto &top_card = pile.back();
        game->drawCard(game->buffer_cr_, x, y, &top_card, true);
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

  // Draw dragged cards
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
        cairo_save(game->buffer_cr_);
        
        // Move to card center for rotation
        cairo_translate(game->buffer_cr_,
                        anim_card.x + game->current_card_width_ / 2,
                        anim_card.y + game->current_card_height_ / 2);
        cairo_rotate(game->buffer_cr_, anim_card.rotation);
        cairo_translate(game->buffer_cr_, -game->current_card_width_ / 2,
                        -game->current_card_height_ / 2);
        
        // Draw the card
        game->drawCard(game->buffer_cr_, 0, 0, &anim_card.card, true);
        
        cairo_restore(game->buffer_cr_);
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

  // Copy buffer to window
  cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

