#include "pyramid.h"
#include <gtk/gtk.h>

// Pyramid Solitaire - Complete Keyboard input handling
// Arrow keys navigate cards, Enter to select/pair, Space for stock

static int location=0;

gboolean PyramidGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                   gpointer data) {
  (void)widget;  // Unused parameter
  PyramidGame *game = static_cast<PyramidGame *>(data);

  if (game->win_animation_active_) {
    game->stopWinAnimation();
    return TRUE;
  }

  bool ctrl_pressed = (event->state & GDK_CONTROL_MASK);

  // Block keyboard during certain animations
  if (game->foundation_move_animation_active_ ||
      game->stock_to_waste_animation_active_ || game->deal_animation_active_ ||
      game->win_animation_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      return TRUE;
    }
    return FALSE;
  }

  switch (event->keyval) {
  case GDK_KEY_F11:
    game->toggleFullscreen();
    return TRUE;

  case GDK_KEY_Escape:
    if (game->is_fullscreen_ && !game->keyboard_navigation_active_) {
      game->toggleFullscreen();
      return TRUE;
    }

    // Deselect any selected card
    if (game->keyboard_selection_active_ || game->keyboard_navigation_active_) {
      game->resetKeyboardNavigation();
      game->refreshDisplay();
      return TRUE;
    }
    break;

  case GDK_KEY_n:
  case GDK_KEY_N:
    if (ctrl_pressed) {
      game->initializeGame();
      game->refreshDisplay();
      return TRUE;
    }
    break;

  case GDK_KEY_q:
  case GDK_KEY_Q:
    if (ctrl_pressed) {
      gtk_main_quit();
      return TRUE;
    }
    break;

  case GDK_KEY_s:
  case GDK_KEY_S:
    if (ctrl_pressed) {
      game->sound_enabled_ = !game->sound_enabled_;
      game->refreshDisplay();
      return TRUE;
    }
    // Plain S: Draw from stock
    game->handleStockPileKeyboard();
    return TRUE;

  case GDK_KEY_space:
    // Draw from stock
    game->handleStockPileKeyboard();
    return TRUE;

  case GDK_KEY_Up:
  case GDK_KEY_w:
  case GDK_KEY_W: {
    // Navigate up through pyramid (toward top)
    game->navigateKeyboard(-1);
    auto loc = game->getPileLocation(location);
    printf("getPileLocation(%d) -> x=%d y=%d\n",
           location, loc.first, loc.second);
    location--;
    return TRUE;
  }

  case GDK_KEY_Down: {
    // Navigate down through pyramid (toward bottom)
    game->navigateKeyboard(1);
    auto loc = game->getPileLocation(location);
    printf("getPileLocation(%d) -> x=%d y=%d\n",
           location, loc.first, loc.second);
    location++;
    return TRUE;
  }

  case GDK_KEY_Left:
  case GDK_KEY_a:
  case GDK_KEY_A:
    // Navigate left in pyramid row
    game->navigateKeyboard(-1);
    return TRUE;

  case GDK_KEY_Right:
  case GDK_KEY_d:
  case GDK_KEY_D:
    // Navigate right in pyramid row
    game->navigateKeyboard(1);
    return TRUE;

  case GDK_KEY_Tab:
    // Tab: cycle through different sections (stock, waste, pyramid)
    game->cycleFocusKeyboard();
    return TRUE;

  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    // Enter: select card or attempt pairing
    game->handleCardSelectionKeyboard();
    return TRUE;

  case GDK_KEY_h:
  case GDK_KEY_H:
    if (ctrl_pressed) {
      game->showKeyboardHelp();
      return TRUE;
    }
    break;

  case GDK_KEY_o:
  case GDK_KEY_O:
    if (ctrl_pressed) {
      // CTRL+O: Load test layout with one King (for easy testing)
      game->dealTestLayout();
      game->refreshDisplay();
      return TRUE;
    }
    break;

  default:
    break;
  }

  return FALSE;
}

void PyramidGame::toggleFullscreen() {
  if (is_fullscreen_) {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = true;
  }
}

void PyramidGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}

// ============================================================================
// KEYBOARD NAVIGATION FUNCTIONS
// ============================================================================

void PyramidGame::handleStockPileKeyboard() {
  // Draw one card from stock to waste pile
  if (!stock_.empty()) {
    playSound(GameSoundEvent::CardFlip);
    waste_.push_back(stock_.back());
    stock_.pop_back();
    refreshDisplay();
  }
  // If stock is empty, just don't play a sound and don't draw
}

void PyramidGame::navigateKeyboard(int direction) {
  printf("Navigate Keyboard called\n");
  // Calculate pile indices (must match animation.cpp)
  int max_foundation_index = 2 + static_cast<int>(foundation_.size()) - 1;
  int first_tableau_index = max_foundation_index + 1;
  
  // Initialize keyboard navigation if not already active
  if (!keyboard_navigation_active_) {
    keyboard_navigation_active_ = true;
    // Start at first available card in stock or waste
    if (!stock_.empty()) {
      selected_pile_ = 0;  // Stock pile
      selected_card_idx_ = 0;
    } else if (!waste_.empty()) {
      selected_pile_ = 1;  // Waste pile
      selected_card_idx_ = static_cast<int>(waste_.size()) - 1;  // Top card
    }
    refreshDisplay();
    return;
  }

  // Move within current section or to next section
  int new_pile = selected_pile_;
  int new_card_idx = selected_card_idx_;

  if (selected_pile_ == 0) {
    // In stock pile - move forward (down)
    if (direction > 0) {
      // Try to move to waste first
      if (!waste_.empty()) {
        new_pile = 1;
        new_card_idx = static_cast<int>(waste_.size()) - 1;
      } else if (!tableau_.empty()) {
        // If waste is empty, skip to pyramid
        new_pile = first_tableau_index;
        new_card_idx = 0;
      }
    }
    // If direction < 0, stay at stock (nowhere to go back)
  } else if (selected_pile_ == 1) {
    // In waste pile
    if (direction < 0 && !stock_.empty()) {
      // Move back to stock
      new_pile = 0;
      new_card_idx = 0;
    } else if (direction > 0) {
      // Move forward to pyramid
      if (!tableau_.empty()) {
        new_pile = first_tableau_index;
        new_card_idx = 0;
      }
    }
  } else if (selected_pile_ >= first_tableau_index) {
    // In pyramid (tableau piles)
    int tableau_idx = selected_pile_ - first_tableau_index;
    
    if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
      // Handle vertical navigation (up/down to different rows)
      if (direction > 0) {
        // Move down (toward bottom row, more cards)
        if (tableau_idx < static_cast<int>(tableau_.size()) - 1) {
          new_pile++;
          // When moving to a row with more cards, stay at same card index or clamp to max
          int next_row_cards = tableau_idx + 2;  // Next row has one more card
          new_card_idx = std::min(selected_card_idx_, next_row_cards - 1);
        }
      } else if (direction < 0) {
        // Move up (toward top row, fewer cards)
        if (tableau_idx > 0) {
          new_pile--;
          // When moving to a row with fewer cards, clamp card index
          int prev_row_cards = tableau_idx;  // Previous row has one fewer card
          new_card_idx = std::min(selected_card_idx_, prev_row_cards - 1);
        } else {
          // From top row to waste
          if (!waste_.empty()) {
            new_pile = 1;
            new_card_idx = static_cast<int>(waste_.size()) - 1;
          } else if (!stock_.empty()) {
            // If waste is empty, go to stock
            new_pile = 0;
            new_card_idx = 0;
          }
        }
      }
    }
  }

  // Validate new position - only update if we actually moved to a different position
  if (new_pile != selected_pile_ || 
      (new_pile == selected_pile_ && new_card_idx != selected_card_idx_)) {
    if (isValidNavigationTarget(new_pile, new_card_idx)) {
      selected_pile_ = new_pile;
      selected_card_idx_ = new_card_idx;
    }
  }

  refreshDisplay();
}

void PyramidGame::cycleFocusKeyboard() {
  if (!keyboard_navigation_active_) {
    keyboard_navigation_active_ = true;
    if (!stock_.empty()) {
      selected_pile_ = 0;
      selected_card_idx_ = 0;
    }
    refreshDisplay();
    return;
  }

  // Cycle: stock -> waste -> pyramid -> stock
  if (selected_pile_ == 0) {
    // From stock to waste
    if (!waste_.empty()) {
      selected_pile_ = 1;
      selected_card_idx_ = static_cast<int>(waste_.size()) - 1;
    }
  } else if (selected_pile_ == 1) {
    // From waste to pyramid
    if (!tableau_.empty()) {
      selected_pile_ = 2;
      selected_card_idx_ = 0;
    }
  } else {
    // From pyramid back to stock
    selected_pile_ = 0;
    selected_card_idx_ = 0;
  }

  refreshDisplay();
}

void PyramidGame::handleCardSelectionKeyboard() {
  if (!keyboard_navigation_active_) {
    keyboard_navigation_active_ = true;
    refreshDisplay();
    return;
  }

  // Get the selected card
  cardlib::Card selected_card;
  bool valid_selection = false;

  if (selected_pile_ == 0 && !stock_.empty()) {
    // Selecting from stock - draw a card
    handleStockPileKeyboard();
    return;
  } else if (selected_pile_ == 1 && !waste_.empty()) {
    // Waste pile - top card
    selected_card = waste_.back();
    valid_selection = true;
  } else if (selected_pile_ >= 2) {
    // Pyramid tableau
    int tableau_idx = selected_pile_ - 2;
    if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
      const auto &pile = tableau_[tableau_idx];
      if (!pile.empty() && selected_card_idx_ < static_cast<int>(pile.size())) {
        const auto &tableau_card = pile[selected_card_idx_];
        if (!tableau_card.removed && tableau_card.face_up) {
          // Check if card is accessible (not blocked)
          if (isTableauCardAccessible(tableau_idx, selected_card_idx_)) {
            selected_card = tableau_card.card;
            valid_selection = true;
          }
        }
      }
    }
  }

  if (!valid_selection) {
    // Card cannot be selected - silently fail
    return;
  }

  // If no card is selected yet, select this one
  if (!keyboard_selection_active_) {
    source_pile_ = selected_pile_;
    source_card_idx_ = selected_card_idx_;
    keyboard_selection_active_ = true;
    playSound(GameSoundEvent::CardFlip);
    refreshDisplay();
    return;
  }

  // A card is already selected - try to pair
  cardlib::Card first_card;
  bool first_valid = false;

  if (source_pile_ == 1 && !waste_.empty()) {
    first_card = waste_.back();
    first_valid = true;
  } else if (source_pile_ >= 2) {
    int tableau_idx = source_pile_ - 2;
    if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
      const auto &pile = tableau_[tableau_idx];
      if (!pile.empty() && source_card_idx_ < static_cast<int>(pile.size())) {
        const auto &tableau_card = pile[source_card_idx_];
        if (!tableau_card.removed) {
          first_card = tableau_card.card;
          first_valid = true;
        }
      }
    }
  }

  if (!first_valid) {
    resetKeyboardNavigation();
    refreshDisplay();
    return;
  }

  // Check if cards can be paired
  int rank1 = static_cast<int>(first_card.rank);
  int rank2 = static_cast<int>(selected_card.rank);

  bool is_king = rank1 == 13;  // King
  bool is_valid_pair = (rank1 + rank2) == 13 || is_king;

  if (is_valid_pair) {
    // Remove the paired cards
    playSound(GameSoundEvent::CardPlace);

    // Remove first card
    if (source_pile_ == 1 && !waste_.empty()) {
      waste_.pop_back();
    } else if (source_pile_ >= 2) {
      int tableau_idx = source_pile_ - 2;
      if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
        tableau_[tableau_idx][source_card_idx_].removed = true;
      }
    }

    // Remove second card (the currently selected one)
    if (selected_pile_ == 1 && !waste_.empty()) {
      waste_.pop_back();
    } else if (selected_pile_ >= 2) {
      int tableau_idx = selected_pile_ - 2;
      if (tableau_idx >= 0 && tableau_idx < static_cast<int>(tableau_.size())) {
        tableau_[tableau_idx][selected_card_idx_].removed = true;
      }
    }

    // Check for win
    checkWinCondition();
    resetKeyboardNavigation();
  } else {
    // Invalid pair - try selecting this card instead (no sound needed)
    source_pile_ = selected_pile_;
    source_card_idx_ = selected_card_idx_;
  }

  refreshDisplay();
}

bool PyramidGame::isValidNavigationTarget(int pile_index, int card_idx) {
  // Calculate pile indices
  int max_foundation_index = 2 + static_cast<int>(foundation_.size()) - 1;
  int first_tableau_index = max_foundation_index + 1;
  
  if (pile_index == 0) {
    // Stock pile
    return !stock_.empty();
  } else if (pile_index == 1) {
    // Waste pile
    return !waste_.empty();
  } else if (pile_index >= 2 && pile_index <= max_foundation_index) {
    // Foundation piles (not normally navigated to in pyramid solitaire)
    return false;
  } else if (pile_index >= first_tableau_index) {
    // Pyramid tableau
    int tableau_idx = pile_index - first_tableau_index;
    if (tableau_idx < 0 || tableau_idx >= static_cast<int>(tableau_.size())) {
      return false;
    }
    const auto &pile = tableau_[tableau_idx];
    if (pile.empty()) {
      return false;  // Can't navigate to empty rows
    }
    // Check if card index is valid and not removed
    if (card_idx < 0 || card_idx >= static_cast<int>(pile.size())) {
      return false;
    }
    // Card must not be removed and must be accessible
    return !pile[card_idx].removed && isTableauCardAccessible(tableau_idx, card_idx);
  }
  return false;
}

bool PyramidGame::isTableauCardAccessible(int row_idx, int card_idx) {
  if (row_idx < 0 || row_idx >= static_cast<int>(tableau_.size())) {
    return false;
  }

  const auto &row = tableau_[row_idx];
  if (card_idx < 0 || card_idx >= static_cast<int>(row.size())) {
    return false;
  }

  // Bottom row is always accessible
  if (row_idx == static_cast<int>(tableau_.size()) - 1) {
    return true;
  }

  // Check if both cards below are removed
  const auto &row_below = tableau_[row_idx + 1];
  
  // Card at same position below
  bool left_below_removed = (card_idx >= static_cast<int>(row_below.size())) || 
                            row_below[card_idx].removed;
  
  // Card at position+1 below
  bool right_below_removed = (card_idx + 1 >= static_cast<int>(row_below.size())) || 
                             row_below[card_idx + 1].removed;

  return left_below_removed && right_below_removed;
}

void PyramidGame::showKeyboardHelp() {
  std::string help =
      "PYRAMID SOLITAIRE - KEYBOARD CONTROLS\n"
      "=====================================\n\n"
      "Navigation:\n"
      "  Arrow Keys / WASD - Navigate through cards\n"
      "  Tab - Cycle through sections (Stock -> Waste -> Pyramid)\n\n"
      "Card Operations:\n"
      "  Enter - Select card or attempt pairing\n"
      "  Space / S - Draw from stock pile\n"
      "  Escape - Deselect/Cancel\n\n"
      "Game Control:\n"
      "  Ctrl+N - New Game\n"
      "  Ctrl+S - Toggle Sound\n"
      "  Ctrl+O - Test Layout (One King)\n"
      "  F11 - Toggle Fullscreen\n"
      "  Ctrl+Q - Quit\n\n"
      "How to Play:\n"
      "  1. Select a card (Enter)\n"
      "  2. Navigate to another card\n"
      "  3. Press Enter again to pair them\n"
      "  4. Cards must sum to 13 (A=1, Q=12, K=13)\n"
      "  5. Kings can be removed alone\n"
      "  6. Clear the entire pyramid to win!";

  g_print("%s\n", help.c_str());
}
