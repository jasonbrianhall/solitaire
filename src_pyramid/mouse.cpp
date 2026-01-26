#include "pyramid.h"
#include <iostream>

// Pyramid Solitaire mouse input handler
// This file handles all mouse interactions: card selection and pairing

gboolean PyramidGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

  // Block interactions during animations
  if (game->win_animation_active_ || game->deal_animation_active_) {
    return TRUE;
  }

  if (event->button == 1) { // Left click
    auto [pile_id, _] = game->getPileAt(event->x, event->y);

    if (pile_id == 0) { // Stock pile
      game->handleStockPileClick();
      return TRUE;
    }

    if (pile_id == 1) { // Waste pile
      if (!game->waste_.empty()) {
        game->selectWasteCard();
      }
      return TRUE;
    }

    if (pile_id >= 2) { // Pyramid card (encoded as row*10 + col + 2)
      int pyramid_id = pile_id - 2;
      int row = pyramid_id / 10;
      int col = pyramid_id % 10;

      if (row >= 0 && row < 7 && col >= 0 && col <= row) {
        game->selectPyramidCard(row, col);
      }
      return TRUE;
    }
  } else if (event->button == 3) { // Right click - deselect
    game->selected_card_row_ = -1;
    game->selected_card_col_ = -1;
    game->card_selected_ = false;
    game->refreshDisplay();
    return TRUE;
  }

  return TRUE;
}

gboolean PyramidGame::onButtonRelease(GtkWidget *widget, GdkEventButton *event,
                                       gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->keyboard_navigation_active_ = false;

  // In Pyramid Solitaire, we don't use drag-and-drop
  // All interactions are click-based

  return TRUE;
}

gboolean PyramidGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                      gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  // In Pyramid Solitaire, motion events are not used
  // Could add hover effects here if desired

  return TRUE;
}

// ============================================================================
// CARD SELECTION AND REMOVAL LOGIC
// ============================================================================

void PyramidGame::selectPyramidCard(int row, int col) {
  // Validate position
  if (row < 0 || row >= 7 || col < 0 || col > row) {
    return;
  }

  // Check if card is already removed
  if (pyramid_[row][col].removed) {
    return;
  }

  // Check if card is exposed (can be selected)
  if (!isCardExposed(row, col)) {
    playSound(GameSoundEvent::NoMatch);
    return;
  }

  const cardlib::Card &card = pyramid_[row][col].card;

  if (!card_selected_) {
    // First card selection
    selected_card_row_ = row;
    selected_card_col_ = col;
    card_selected_ = true;
    playSound(GameSoundEvent::CardFlip);
  } else {
    // Second card clicked

    if (selected_card_row_ == row && selected_card_col_ == col) {
      // Same card clicked twice
      if (canRemoveKing(card)) {
        // King: remove on second click
        removeKing(row, col);
        playSound(GameSoundEvent::CardPlace);

        if (checkWinCondition()) {
          startWinAnimation();
        }
      } else {
        // Non-King: deselect
        playSound(GameSoundEvent::NoMatch);
      }

      selected_card_row_ = -1;
      selected_card_col_ = -1;
      card_selected_ = false;
    } else {
      // Different card clicked
      const cardlib::Card &selected = pyramid_[selected_card_row_][selected_card_col_].card;

      if (canRemovePair(selected, card)) {
        // Valid pair - remove both
        removePair(selected_card_row_, selected_card_col_, row, col);
        playSound(GameSoundEvent::CardPlace);

        if (checkWinCondition()) {
          startWinAnimation();
        }
      } else {
        // Invalid pair
        playSound(GameSoundEvent::NoMatch);
      }

      // Reset selection
      selected_card_row_ = -1;
      selected_card_col_ = -1;
      card_selected_ = false;
    }
  }

  refreshDisplay();
}

void PyramidGame::selectWasteCard() {
  if (!waste_.empty()) {
    playSound(GameSoundEvent::CardFlip);
    // In basic Pyramid, you don't directly use waste cards
    // This could be extended for variant rules
  }
}

void PyramidGame::handleStockPileClick() {
  if (stock_.empty()) {
    // Stock is empty - game continues with pyramid cards only
    playSound(GameSoundEvent::NoMatch);
  } else {
    // Draw one card from stock to waste
    waste_.push_back(stock_.back());
    stock_.pop_back();
    playSound(GameSoundEvent::DealCard);
    refreshDisplay();
  }
}

void PyramidGame::removePair(int row1, int col1, int row2, int col2) {
  if (row1 >= 0 && row1 < 7 && col1 >= 0 && col1 <= row1 &&
      row2 >= 0 && row2 < 7 && col2 >= 0 && col2 <= row2) {
    pyramid_[row1][col1].removed = true;
    pyramid_[row2][col2].removed = true;
  }
}

void PyramidGame::removeKing(int row, int col) {
  if (row >= 0 && row < 7 && col >= 0 && col <= row) {
    pyramid_[row][col].removed = true;
  }
}

// ============================================================================
// CARD EXPOSURE AND VALIDATION
// ============================================================================

bool PyramidGame::isCardExposed(int row, int col) const {
  // Check bounds
  if (row < 0 || row >= 7 || col < 0 || col > row) {
    return false;
  }

  // Already removed cards are not exposed
  if (pyramid_[row][col].removed) {
    return false;
  }

  // Bottom row (row 6) is always exposed
  if (row == 6) {
    return true;
  }

  // For other rows, both cards below must be removed
  bool left_removed = pyramid_[row + 1][col].removed;
  bool right_removed = pyramid_[row + 1][col + 1].removed;

  return left_removed && right_removed;
}

bool PyramidGame::canRemovePair(const cardlib::Card &card1,
                                 const cardlib::Card &card2) const {
  int value1 = getCardValue(card1);
  int value2 = getCardValue(card2);

  return (value1 + value2) == 13;
}

bool PyramidGame::canRemoveKing(const cardlib::Card &card) const {
  return card.rank == cardlib::Rank::KING;
}

int PyramidGame::getCardValue(const cardlib::Card &card) const {
  switch (card.rank) {
    case cardlib::Rank::ACE:
      return 1;
    case cardlib::Rank::TWO:
      return 2;
    case cardlib::Rank::THREE:
      return 3;
    case cardlib::Rank::FOUR:
      return 4;
    case cardlib::Rank::FIVE:
      return 5;
    case cardlib::Rank::SIX:
      return 6;
    case cardlib::Rank::SEVEN:
      return 7;
    case cardlib::Rank::EIGHT:
      return 8;
    case cardlib::Rank::NINE:
      return 9;
    case cardlib::Rank::TEN:
      return 10;
    case cardlib::Rank::JACK:
      return 11;
    case cardlib::Rank::QUEEN:
      return 12;
    case cardlib::Rank::KING:
      return 13;
    default:
      return 0;
  }
}

std::pair<int, int> PyramidGame::getPileAt(int x, int y) const {
  int pyramid_start_y = 100;

  // Check pyramid positions
  for (int row = 0; row < 7; row++) {
    int row_width = (row + 1) * (current_card_width_ + current_card_spacing_);
    int row_x = (game_area_width_ - row_width) / 2; // Center horizontally

    for (int col = 0; col <= row; col++) {
      int card_x = row_x + col * (current_card_width_ + current_card_spacing_);
      int card_y = pyramid_start_y + row * (current_card_height_ + current_vert_spacing_);

      // Check if click is on this card
      if (x >= card_x && x < card_x + current_card_width_ &&
          y >= card_y && y < card_y + current_card_height_) {
        // Encode as (row*10 + col + 2) to avoid conflicts with stock(0) and waste(1)
        return {row * 10 + col + 2, 0};
      }
    }
  }

  // Check stock pile position (top right)
  int stock_x = game_area_width_ - current_card_width_ - current_card_spacing_;
  int stock_y = current_card_spacing_;

  if (x >= stock_x && x < stock_x + current_card_width_ &&
      y >= stock_y && y < stock_y + current_card_height_) {
    return {0, 0}; // Stock pile
  }

  // Check waste pile position (next to stock)
  int waste_x = stock_x - current_card_width_ - current_card_spacing_;

  if (x >= waste_x && x < waste_x + current_card_width_ &&
      y >= stock_y && y < stock_y + current_card_height_) {
    return {1, 0}; // Waste pile
  }

  // No hit
  return {-1, -1};
}
