#include "pyramid.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

void PyramidGame::drawCardFragment(cairo_t *cr,
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

gboolean PyramidGame::onDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  // Get the widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);

  // Create or resize buffer surface if needed
  game->initializeOrResizeBuffer(allocation.width, allocation.height);

  // Clear buffer with background color
  cairo_set_source_rgb(game->buffer_cr_, 0.0, 0.6, 0.0);
  cairo_paint(game->buffer_cr_);

  // Draw main game components in order
  game->drawStockPile();
  game->drawWastePile();
  game->drawDiscardPile();  // Discard pile for matched cards
  game->drawFoundationPiles();
  game->drawTableauPiles();
  
  // Draw animations and dragged cards
  game->drawAllAnimations();
  
  // Draw keyboard navigation highlight if active
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

// Initialize or resize the drawing buffer as needed
void PyramidGame::initializeOrResizeBuffer(int width, int height) {
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

// Draw foundation pile during win animation
void PyramidGame::drawFoundationDuringWinAnimation(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y) {
  // Only draw the topmost non-animated card
  for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
    if (!animated_foundation_cards_[pile_index][j]) {
      drawCard(buffer_cr_, x, y, &pile[j], true);
      break;
    }
  }
}

// Draw foundation pile during normal gameplay
void PyramidGame::drawNormalFoundationPile(size_t pile_index, const std::vector<cardlib::Card> &pile, int x, int y) {
  // Check if the top card is being dragged from foundation
  bool top_card_dragging =
      (dragging_ && drag_source_pile_ == pile_index + 2 &&
       !pile.empty() && drag_cards_.size() == 1 &&
       drag_cards_[0].suit == pile.back().suit &&
       drag_cards_[0].rank == pile.back().rank);

  if (!top_card_dragging) {
    const auto &top_card = pile.back();
    drawCard(buffer_cr_, x, y, &top_card, true);
  } else if (pile.size() > 1) {
    // Draw the second-to-top card
    const auto &second_card = pile[pile.size() - 2];
    drawCard(buffer_cr_, x, y, &second_card, true);
  }
}

// Draw tableau piles during normal gameplay
// Fix to prevent tableau cards from disappearing when dragging foundation cards
void PyramidGame::drawNormalTableauPile(size_t pile_index, const std::vector<TableauCard> &pile, int x, int base_y) {
  // Calculate max foundation index and first tableau index
  int max_foundation_index = 2 + foundation_.size() - 1;
  int first_tableau_index = max_foundation_index + 1;
  
  for (size_t j = 0; j < pile.size(); j++) {
    // Skip cards that are being dragged, but ONLY if they're from THIS tableau pile
    // This ensures other tableau piles are always drawn
    if (dragging_ && 
        drag_source_pile_ >= first_tableau_index && 
        drag_source_pile_ - first_tableau_index == static_cast<int>(pile_index) &&
        j >= pile.size() - drag_cards_.size()) {
      continue;
    }

    int current_y = base_y + j * current_vert_spacing_;
    const auto &tableau_card = pile[j];
    drawCard(buffer_cr_, x, current_y, &tableau_card.card, tableau_card.face_up);
  }
}

// Draw all active animations and dragged cards
void PyramidGame::drawAllAnimations() {
  // Draw stock to waste animation
  if (stock_to_waste_animation_active_) {
    drawAnimatedCard(buffer_cr_, stock_to_waste_card_);
  }
  
  // Draw cards being dragged
  if (dragging_ && !drag_cards_.empty()) {
    drawDraggedCards();
  }

  // Draw win animation
  if (win_animation_active_) {
    drawWinAnimation();
  }

  // Draw foundation move animation
  if (foundation_move_animation_active_) {
    drawAnimatedCard(buffer_cr_, foundation_move_card_);
  }

  // Draw deal animation
  if (deal_animation_active_) {
    drawDealAnimation();
  }
}

// Draw cards that are currently being dragged
void PyramidGame::drawDraggedCards() {
  int drag_x = static_cast<int>(drag_start_x_ - drag_offset_x_);
  int drag_y = static_cast<int>(drag_start_y_ - drag_offset_y_);

  for (size_t i = 0; i < drag_cards_.size(); i++) {
    drawCard(buffer_cr_, drag_x,
            drag_y + i * current_vert_spacing_,
            &drag_cards_[i], true);
  }
}

// Draw the win animation effects
void PyramidGame::drawWinAnimation() {
  for (const auto &anim_card : animated_cards_) {
    if (!anim_card.active)
      continue;

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

// Draw the deal animation
void PyramidGame::drawDealAnimation() {
  // Debug indicator - small red square to indicate deal animation is active
  cairo_set_source_rgb(buffer_cr_, 1.0, 0.0, 0.0);
  cairo_rectangle(buffer_cr_, 10, 10, 10, 10);
  cairo_fill(buffer_cr_);

  for (const auto &anim_card : deal_cards_) {
    if (anim_card.active) {
      drawAnimatedCard(buffer_cr_, anim_card);
    }
  }
}

void PyramidGame::explodeCard(AnimatedCard &card) {
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

void PyramidGame::startDealAnimation() {
  if (deal_animation_active_)
    return;

#ifdef DEBUG
  std::cout << "Starting deal animation" << std::endl;
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

  // CRITICAL FIX: Determine which rendering surface is ACTUALLY visible
  // Don't just trust rendering_engine_ - check which widget is in the stack
  bool use_opengl_callback = false;
  
  if (rendering_stack_) {
    GtkWidget *visible_child = gtk_stack_get_visible_child(GTK_STACK(rendering_stack_));
    // Use OpenGL callback ONLY if gl_area_ is visible and initialized
    use_opengl_callback = (visible_child == gl_area_ && gl_area_ != nullptr);
  }

      animation_timer_id_ =
          g_timeout_add(ANIMATION_INTERVAL, onDealAnimationTick, this);
      dealNextCard();

  // Force a redraw to ensure we don't see the cards already in place
  refreshDisplay();
}

gboolean PyramidGame::onDealAnimationTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->updateDealAnimation();
  return game->deal_animation_active_ ? TRUE : FALSE;
}

void PyramidGame::updateDealAnimation() {
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
      playSound(GameSoundEvent::CardPlace);
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

void PyramidGame::dealNextCard() {
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

  playSound(tableau_[pile_index][card_index].face_up ? 
            GameSoundEvent::CardFlip : 
            GameSoundEvent::DealCard);

  // Add to animation list
  deal_cards_.push_back(anim_card);
  cards_dealt_++;
}

void PyramidGame::startFoundationMoveAnimation(const cardlib::Card &card, int source_pile, int source_index, int target_pile) {
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

gboolean PyramidGame::onFoundationMoveAnimationTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->updateFoundationMoveAnimation();
  return game->foundation_move_animation_active_ ? TRUE : FALSE;
}

void PyramidGame::updateFoundationMoveAnimation() {
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

void PyramidGame::drawAnimatedCard(cairo_t *cr,
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

void PyramidGame::startStockToWasteAnimation() {
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

gboolean PyramidGame::onStockToWasteAnimationTick(gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->updateStockToWasteAnimation();
  return game->stock_to_waste_animation_active_ ? TRUE : FALSE;
}

void PyramidGame::updateStockToWasteAnimation() {
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

void PyramidGame::completeStockToWasteAnimation() {
  stock_to_waste_animation_active_ = false;

  if (animation_timer_id_ > 0) {
    g_source_remove(animation_timer_id_);
    animation_timer_id_ = 0;
  }

  pending_waste_cards_.clear();
  refreshDisplay();
}

// Highlight the selected card in the onDraw method
void PyramidGame::highlightSelectedCard(cairo_t *cr) {
  int x = 0, y = 0;

  if (!cr || selected_pile_ == -1) {
    return;
  }

  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  
  // Validate keyboard selection
  if (keyboard_selection_active_) {
    bool invalid_source = false;
    
    if (source_pile_ < 0) {
      invalid_source = true;
    } else if (source_pile_ >= first_tableau_index) {
      int tableau_idx = source_pile_ - first_tableau_index;
      if (tableau_idx < 0 || tableau_idx >= tableau_.size()) {
        invalid_source = true;
      }
    } else if (source_pile_ == 1 && waste_.empty()) {
      invalid_source = true;
    } else if (source_pile_ >= 2 && source_pile_ <= max_foundation_index) {
      int foundation_idx = source_pile_ - 2;
      if (foundation_idx < 0 || foundation_idx >= foundation_.size()) {
        invalid_source = true;
      }
    }
    
    if (invalid_source) {
      // Invalid source pile, reset selection state
      keyboard_selection_active_ = false;
      source_pile_ = -1;
      source_card_idx_ = -1;
      return;
    }
  }

  // Determine position based on pile type
  if (selected_pile_ == 0) {
    // Stock pile
    x = current_card_spacing_;
    y = current_card_spacing_;
  } else if (selected_pile_ == 1) {
    // Waste pile
    x = 2 * current_card_spacing_ + current_card_width_;
    y = current_card_spacing_;
  } else if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
    // Foundation piles - critical fix here
    int foundation_idx = selected_pile_ - 2;
    
    // Make sure foundation_idx is valid
    if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
      // Match the exact calculation from drawFoundationPiles()
      x = 3 * (current_card_width_ + current_card_spacing_) + 
          foundation_idx * (current_card_width_ + current_card_spacing_);
      y = current_card_spacing_;
    }
  } else if (selected_pile_ >= first_tableau_index) {
    // Tableau piles
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      x = current_card_spacing_ +
          tableau_idx * (current_card_width_ + current_card_spacing_);

      // For empty tableau piles, highlight the empty space
      const auto &tableau_pile = tableau_[tableau_idx];
      if (tableau_pile.empty()) {
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
      } else if (selected_card_idx_ == -1 || selected_card_idx_ >= static_cast<int>(tableau_pile.size())) {
        // If no card is selected or the index is invalid, highlight the top card
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
            (tableau_pile.size() - 1) * current_vert_spacing_;
      } else {
        // Highlight the specific selected card
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
            selected_card_idx_ * current_vert_spacing_;
      }
    }
  }

  // Choose highlight color based on whether we're selecting a card to move
  if (keyboard_selection_active_ && source_pile_ == selected_pile_ &&
      (source_card_idx_ == selected_card_idx_ || selected_card_idx_ == -1)) {
    // Source card/pile is highlighted in blue
    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.5); // Semi-transparent blue
  } else {
    // Regular selection is highlighted in yellow
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.5); // Semi-transparent yellow
  }

  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4,
                  current_card_height_ + 4);
  cairo_stroke(cr);

  // If we have a card selected for movement, highlight all cards below it in a
  // tableau pile
  if (keyboard_selection_active_ && source_pile_ >= first_tableau_index && source_card_idx_ >= 0) {
    int tableau_idx = source_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      auto &tableau_pile = tableau_[tableau_idx];

      if (!tableau_pile.empty() && source_card_idx_ < tableau_pile.size()) {
        // Highlight all cards from the selected one to the bottom
        cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.3); // Lighter blue for stack

        x = current_card_spacing_ +
            tableau_idx * (current_card_width_ + current_card_spacing_);
        y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ +
            source_card_idx_ * current_vert_spacing_;

        // Draw a single rectangle that covers all cards in the stack
        int stack_height =
            (tableau_pile.size() - source_card_idx_ - 1) * current_vert_spacing_ +
            current_card_height_;

        if (stack_height > 0) {
          cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4,
                          stack_height + 4);
          cairo_stroke(cr);
        }
      }
    }
  }
}

PyramidGame::~PyramidGame() {
  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
  }
  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
  }
  cleanupAudio();
}

void PyramidGame::drawCard(cairo_t *cr, int x, int y,
                             const cardlib::Card *card, bool face_up) {
  if (face_up && card) {

    std::string key = std::to_string(static_cast<int>(card->suit)) +
                      std::to_string(static_cast<int>(card->rank));
    auto it = card_surface_cache_.find(key);

    if (it == card_surface_cache_.end()) {
      if (auto img = deck_.getCardImage(*card)) {
        GError *error = nullptr;
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

        if (!gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(),
                                     &error)) {
          if (error)
            g_error_free(error);
          g_object_unref(loader);
          return;
        }

        if (!gdk_pixbuf_loader_close(loader, &error)) {
          if (error)
            g_error_free(error);
          g_object_unref(loader);
          return;
        }

        GdkPixbuf *original_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (original_pixbuf) {
          GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(
              original_pixbuf,
              current_card_width_, // Use current dimensions
              current_card_height_, GDK_INTERP_BILINEAR);

          if (scaled_pixbuf) {
            cairo_surface_t *surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
            cairo_t *surface_cr = cairo_create(surface);

            gdk_cairo_set_source_pixbuf(surface_cr, scaled_pixbuf, 0, 0);
            cairo_paint(surface_cr);
            cairo_destroy(surface_cr);

            card_surface_cache_[key] = surface;

            g_object_unref(scaled_pixbuf);
          }
        }
        g_object_unref(loader);

        it = card_surface_cache_.find(key);
      }
    }

    if (it != card_surface_cache_.end()) {
      // Scale the surface to the current card dimensions
      cairo_save(cr);
      cairo_scale(cr,
                  (double)current_card_width_ /
                      cairo_image_surface_get_width(it->second),
                  (double)current_card_height_ /
                      cairo_image_surface_get_height(it->second));
      cairo_set_source_surface(cr, it->second,
                               x * cairo_image_surface_get_width(it->second) /
                                   current_card_width_,
                               y * cairo_image_surface_get_height(it->second) /
                                   current_card_height_);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  } else {
    auto custom_it = card_surface_cache_.find("custom_back");
    auto default_it = card_surface_cache_.find("back");
    cairo_surface_t *back_surface = nullptr;

    if (!custom_back_path_.empty() && custom_it != card_surface_cache_.end()) {
      back_surface = custom_it->second;
    } else if (default_it != card_surface_cache_.end()) {
      back_surface = default_it->second;
    }

    if (back_surface) {
      // Scale the surface to the current card dimensions
      cairo_save(cr);
      cairo_scale(cr,
                  (double)current_card_width_ /
                      cairo_image_surface_get_width(back_surface),
                  (double)current_card_height_ /
                      cairo_image_surface_get_height(back_surface));
      cairo_set_source_surface(
          cr, back_surface,
          x * cairo_image_surface_get_width(back_surface) / current_card_width_,
          y * cairo_image_surface_get_height(back_surface) /
              current_card_height_);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      // Draw a placeholder rectangle if no back image is available
      cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
      cairo_rectangle(cr, x, y, current_card_width_, current_card_height_);
      cairo_stroke(cr);
    }
  }
}

void PyramidGame::initializeCardCache() {
  // Pre-load all card images into cairo surfaces with current dimensions
  cleanupCardCache();

  for (const auto &card : deck_.getAllCards()) {
    if (auto img = deck_.getCardImage(card)) {
      GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
      gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(),
                              nullptr);
      gdk_pixbuf_loader_close(loader, nullptr);

      GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
      GdkPixbuf *scaled =
          gdk_pixbuf_scale_simple(pixbuf, current_card_width_,
                                  current_card_height_, GDK_INTERP_BILINEAR);

      cairo_surface_t *surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
      cairo_t *cr = cairo_create(surface);
      gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);

      std::string key = std::to_string(static_cast<int>(card.suit)) +
                        std::to_string(static_cast<int>(card.rank));
      card_surface_cache_[key] = surface;

      g_object_unref(scaled);
      g_object_unref(loader);
    }
  }

  // Cache card back
  if (auto back_img = deck_.getCardBackImage()) {
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, back_img->data.data(),
                            back_img->data.size(), nullptr);
    gdk_pixbuf_loader_close(loader, nullptr);

    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
        pixbuf, current_card_width_, current_card_height_, GDK_INTERP_BILINEAR);

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, current_card_width_, current_card_height_);
    cairo_t *cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    card_surface_cache_["back"] = surface;

    g_object_unref(scaled);
    g_object_unref(loader);
  }
}

void PyramidGame::cleanupCardCache() {
  for (auto &[key, surface] : card_surface_cache_) {
    cairo_surface_destroy(surface);
  }
  card_surface_cache_.clear();
}

cairo_surface_t *PyramidGame::getCardSurface(const cardlib::Card &card) {
  std::string key = std::to_string(static_cast<int>(card.suit)) +
                    std::to_string(static_cast<int>(card.rank));
  auto it = card_surface_cache_.find(key);
  return it != card_surface_cache_.end() ? it->second : nullptr;
}

cairo_surface_t *PyramidGame::getCardBackSurface() {
  auto it = card_surface_cache_.find("back");
  return it != card_surface_cache_.end() ? it->second : nullptr;
}

void PyramidGame::cleanupResources() {
  // Clean up Cairo resources
  if (buffer_cr_) {
    cairo_destroy(buffer_cr_);
    buffer_cr_ = nullptr;
  }
  if (buffer_surface_) {
    cairo_surface_destroy(buffer_surface_);
    buffer_surface_ = nullptr;
  }

  // Clean up card cache
  cleanupCardCache();
}

void PyramidGame::clearCustomBack() {
  custom_back_path_.clear();

  // Remove the custom back from cache if it exists
  auto it = card_surface_cache_.find("custom_back");
  if (it != card_surface_cache_.end()) {
    cairo_surface_destroy(it->second);
    card_surface_cache_.erase(it);
  }

  // Update settings file
  saveSettings();
}

void PyramidGame::drawEmptyPile(cairo_t *cr, int x, int y) {
  // Draw a placeholder for an empty pile (cell or foundation)
  cairo_save(cr);
  
  // Draw a rounded rectangle with a thin border
  double radius = 10.0;
  double degrees = G_PI / 180.0;
  
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + current_card_width_ - radius, y + radius, radius, -90 * degrees, 0 * degrees);
  cairo_arc(cr, x + current_card_width_ - radius, y + current_card_height_ - radius, radius, 0 * degrees, 90 * degrees);
  cairo_arc(cr, x + radius, y + current_card_height_ - radius, radius, 90 * degrees, 180 * degrees);
  cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
  cairo_close_path(cr);
  
  // Set a light gray fill with semi-transparency
  cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.5);
  cairo_fill_preserve(cr);
  
  // Set a darker gray border
  cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  
  cairo_restore(cr);
}

void PyramidGame::refreshCardCache() {
  // Clean up existing cache
  cleanupCardCache();

  // Rebuild the cache
  initializeCardCache();

  // If we have a custom back, reload it
  if (!custom_back_path_.empty()) {

    std::ifstream file(custom_back_path_, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      std::vector<char> buffer(size);
      if (file.read(buffer.data(), size)) {
        GError *error = nullptr;
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

        if (gdk_pixbuf_loader_write(loader, (const guchar *)buffer.data(), size,
                                    &error)) {
          gdk_pixbuf_loader_close(loader, &error);

          GdkPixbuf *original_pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
          if (original_pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                original_pixbuf, CARD_WIDTH, CARD_HEIGHT, GDK_INTERP_BILINEAR);

            if (scaled) {
              cairo_surface_t *surface = cairo_image_surface_create(
                  CAIRO_FORMAT_ARGB32, CARD_WIDTH, CARD_HEIGHT);
              cairo_t *surface_cr = cairo_create(surface);

              gdk_cairo_set_source_pixbuf(surface_cr, scaled, 0, 0);
              cairo_paint(surface_cr);
              cairo_destroy(surface_cr);

              card_surface_cache_["custom_back"] = surface;

              g_object_unref(scaled);
            }
          }
        }

        if (error) {
          g_error_free(error);
        }
        g_object_unref(loader);
      }
    }

#ifdef USEOPENGL
    // Also reload custom back texture for OpenGL mode
    reloadCustomCardBackTexture_gl();
#endif
  }
}

void PyramidGame::clearAndRebuildCaches() {
  // Clear and rebuild Cairo card surface cache
  cleanupCardCache();

#ifdef USEOPENGL
  // Clear and rebuild OpenGL card texture cache
  if (cardTextures_gl_.size() > 0) {
    for (auto &[key, texture] : cardTextures_gl_) {
      if (texture != 0) {
        glDeleteTextures(1, &texture);
      }
    }
    cardTextures_gl_.clear();
  }
  
  // Clear OpenGL card back texture
  if (cardBackTexture_gl_ != 0) {
    glDeleteTextures(1, &cardBackTexture_gl_);
    cardBackTexture_gl_ = 0;
  }
  
  // Rebuild OpenGL textures
  initializeCardTextures_gl();
#endif
  
  // Rebuild Cairo cache
  initializeCardCache();
  
  // Reset the cache dirty flag
  cache_dirty_ = false;
  
  // Force complete redraw of the entire screen
  refreshDisplay();
}
